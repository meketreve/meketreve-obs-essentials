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
#include "frame-grab.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace FaceMask {

FrameGrabber::~FrameGrabber()
{
	// GPU objects must be freed under a graphics context; the owner is
	// expected to call release() inside obs_enter_graphics(). Guard anyway.
	if (texrender_ || stage_) {
		obs_enter_graphics();
		release();
		obs_leave_graphics();
	}
}

void FrameGrabber::release()
{
	if (stage_) {
		gs_stagesurface_destroy(stage_);
		stage_ = nullptr;
	}
	if (texrender_) {
		gs_texrender_destroy(texrender_);
		texrender_ = nullptr;
	}
	stage_w_ = stage_h_ = 0;
}

bool FrameGrabber::grab(obs_source_t *target, uint32_t w, uint32_t h, int max_dim, bool flip_v, Image &out_bgr,
			float &out_scale)
{
	if (!target || w == 0 || h == 0)
		return false;

	// Downscaled dimensions (longest side <= max_dim, never upscale).
	float s = (float)max_dim / (float)std::max(w, h);
	if (s > 1.f)
		s = 1.f;
	uint32_t dw = std::max(1u, (uint32_t)std::lround((double)w * s));
	uint32_t dh = std::max(1u, (uint32_t)std::lround((double)h * s));
	out_scale = (float)w / (float)dw;

	if (!texrender_)
		texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(texrender_);

	gs_blend_state_push();
	gs_reset_blend_state();

	bool rendered = false;
	if (gs_texrender_begin(texrender_, dw, dh)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.f, 0);
		// Map full source space (w,h) into the dw x dh viewport => downscale.
		gs_ortho(0.f, (float)w, 0.f, (float)h, -100.f, 100.f);
		obs_source_video_render(target);
		gs_texrender_end(texrender_);
		rendered = true;
	}

	gs_blend_state_pop();
	if (!rendered)
		return false;

	gs_texture_t *tex = gs_texrender_get_texture(texrender_);
	if (!tex)
		return false;

	if (!stage_ || stage_w_ != dw || stage_h_ != dh) {
		if (stage_)
			gs_stagesurface_destroy(stage_);
		stage_ = gs_stagesurface_create(dw, dh, GS_RGBA);
		stage_w_ = dw;
		stage_h_ = dh;
	}

	gs_stage_texture(stage_, tex);

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(stage_, &data, &linesize))
		return false;

	out_bgr = imageFromRgba(data, (int)dw, (int)dh, linesize, flip_v);
	gs_stagesurface_unmap(stage_);
	return true;
}

} // namespace FaceMask
