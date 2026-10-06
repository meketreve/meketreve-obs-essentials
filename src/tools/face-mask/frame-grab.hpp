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

#include <opencv2/core.hpp>

#include <cstdint>

namespace FaceMask {

/* Renders an OBS source into an offscreen texrender, downscales it, stages it
 * to CPU memory and returns a BGR cv::Mat for OpenCV. Lives entirely on the
 * graphics thread (texrender / stagesurface are GPU objects). */
class FrameGrabber {
public:
	~FrameGrabber();

	/* Render `target` downscaled (longest side <= max_dim) and read it back
	 * as BGR. out_scale = factor to multiply detection coords by to map back
	 * to the full (w,h) source. flip_v vertically flips the readback (some
	 * graphics backends stage bottom-up). Returns false on failure.
	 * MUST be called on the graphics thread. */
	bool grab(obs_source_t *target, uint32_t w, uint32_t h, int max_dim, bool flip_v, cv::Mat &out_bgr,
		  float &out_scale);

	/* Free GPU objects. MUST be called inside obs_enter_graphics(). */
	void release();

private:
	gs_texrender_t *texrender_ = nullptr;
	gs_stagesurf_t *stage_ = nullptr;
	uint32_t stage_w_ = 0;
	uint32_t stage_h_ = 0;
};

} // namespace FaceMask
