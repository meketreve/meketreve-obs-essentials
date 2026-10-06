/*
Meketreve OBS Essentials - Face mask
Copyright (C) 2026 meketreve

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#pragma once

#include <obs.h>

#include "pose.hpp"

#include <opencv2/core.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace FaceMask {

/* Tunables for placing/sizing the mask quad, in fractions of the model
 * interocular distance (so on-screen size auto-tracks face size). */
struct MaskParams {
	float scale_x = 2.2f;  // quad half-width  = 0.5 * interocular * scale_x
	float scale_y = 1.2f;  // quad half-height = 0.5 * interocular * scale_y
	float offset_x = 0.0f; // shift along face X (right)
	float offset_y = 0.0f; // shift along face Y (up)
	float depth = 0.3f;    // push toward camera along face normal
	float opacity = 1.0f;
	float rotation_follow = 1.0f; // 0..1, reserved for M4 (blends out rotation)
};

/* Renders an image/video mask as a 3D-projected quad over the face.
 * The mask texture comes from another OBS source (Image/Media Source) chosen by
 * name, so both images and videos work with no custom decoder. */
class MaskRenderer {
public:
	~MaskRenderer();

	bool load_effect(); // graphics thread; idempotent
	bool effect_loaded() const { return effect_ != nullptr; }

	void set_source(const char *name); // any thread

	/* graphics thread: draw the mask using the given pose. No-op if the
	 * pose is invalid, no source is set, or a corner falls behind camera. */
	void render(const HeadPose &pose, const MaskParams &p);

	/* graphics thread: mesh-morph. Draws the mask texture mapped onto the
	 * deformed FaceMesh (mesh = 468 landmark points in screen pixels) via
	 * the canonical triangulation + frontal UV. No-op without enough points
	 * or a source. opacity in [0,1]. */
	void render_mesh(const std::vector<cv::Point2f> &mesh, float opacity);

	void release(); // graphics thread

private:
	gs_texture_t *resolve_mask_texture();  // -> texture or nullptr
	gs_indexbuffer_t *mesh_index_buffer(); // cached triangulation indices

	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *p_image_ = nullptr;
	gs_eparam_t *p_opacity_ = nullptr;

	gs_texrender_t *mask_tr_ = nullptr;
	gs_indexbuffer_t *mesh_ib_ = nullptr; // static FaceMesh triangulation

	std::mutex src_mtx_;
	obs_weak_source_t *weak_src_ = nullptr;
	std::string src_name_; // saved selection; re-resolved lazily (OBS may
			       // load the mask source after this filter).
};

} // namespace FaceMask
