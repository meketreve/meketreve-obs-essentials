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
#include "mask-renderer.hpp"
#include "facemesh-tables.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <array>
#include <cmath>

namespace FaceMask {

MaskRenderer::~MaskRenderer()
{
	if (effect_ || mask_tr_ || mesh_ib_ || weak_src_) {
		obs_enter_graphics();
		release();
		obs_leave_graphics();
	}
}

bool MaskRenderer::load_effect()
{
	if (effect_)
		return true;
	char *path = obs_module_file("face-mask/mask.effect");
	if (!path) {
		obs_log(LOG_ERROR, "[face-mask] mask.effect not found");
		return false;
	}
	char *err = nullptr;
	effect_ = gs_effect_create_from_file(path, &err);
	bfree(path);
	if (!effect_) {
		obs_log(LOG_ERROR, "[face-mask] failed to compile mask.effect: %s", err ? err : "(unknown)");
		bfree(err);
		return false;
	}
	bfree(err);
	p_image_ = gs_effect_get_param_by_name(effect_, "image");
	p_opacity_ = gs_effect_get_param_by_name(effect_, "opacity");
	return true;
}

void MaskRenderer::set_source(const char *name)
{
	const std::string nn = (name && *name) ? name : "";
	std::lock_guard<std::mutex> lk(src_mtx_);
	if (nn == src_name_)
		return; // unchanged; keep any resolved handle
	src_name_ = nn;
	// Drop the old handle; resolve_mask_texture() re-resolves by name on the
	// graphics thread (the target source may not exist yet at load time).
	if (weak_src_) {
		obs_weak_source_release(weak_src_);
		weak_src_ = nullptr;
	}
}

void MaskRenderer::release()
{
	{
		std::lock_guard<std::mutex> lk(src_mtx_);
		if (weak_src_) {
			obs_weak_source_release(weak_src_);
			weak_src_ = nullptr;
		}
	}
	if (mask_tr_) {
		gs_texrender_destroy(mask_tr_);
		mask_tr_ = nullptr;
	}
	if (mesh_ib_) {
		gs_indexbuffer_destroy(mesh_ib_);
		mesh_ib_ = nullptr;
	}
	if (effect_) {
		gs_effect_destroy(effect_);
		effect_ = nullptr;
		p_image_ = p_opacity_ = nullptr;
	}
}

gs_texture_t *MaskRenderer::resolve_mask_texture()
{
	obs_source_t *src = nullptr;
	{
		std::lock_guard<std::mutex> lk(src_mtx_);
		// Lazy resolve: the selected source may have loaded after this
		// filter (OBS scene-load order), so retry by name until found.
		if (!weak_src_ && !src_name_.empty()) {
			obs_source_t *s = obs_get_source_by_name(src_name_.c_str());
			if (s) {
				weak_src_ = obs_source_get_weak_source(s);
				obs_source_release(s);
			}
		}
		if (weak_src_)
			src = obs_weak_source_get_source(weak_src_);
	}
	if (!src)
		return nullptr;

	uint32_t mw = obs_source_get_width(src);
	uint32_t mh = obs_source_get_height(src);
	if (mw == 0 || mh == 0) {
		obs_source_release(src);
		return nullptr;
	}

	if (!mask_tr_)
		mask_tr_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(mask_tr_);

	gs_blend_state_push();
	gs_reset_blend_state();
	bool ok = false;
	if (gs_texrender_begin(mask_tr_, mw, mh)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.f, 0);
		gs_ortho(0.f, (float)mw, 0.f, (float)mh, -100.f, 100.f);
		obs_source_video_render(src);
		gs_texrender_end(mask_tr_);
		ok = true;
	}
	gs_blend_state_pop();
	obs_source_release(src);

	if (!ok)
		return nullptr;
	return gs_texrender_get_texture(mask_tr_);
}

void MaskRenderer::render(const HeadPose &pose, const MaskParams &p)
{
	if (!pose.valid || !effect_)
		return;

	gs_texture_t *tex = resolve_mask_texture();
	if (!tex)
		return;

	// Quad corners in head-model object space.
	const double io = facemodel::interocular();
	const Vec3d X = facemodel::axis_right();
	const Vec3d Y = facemodel::axis_up();
	const Vec3d N = facemodel::toward_camera();
	const Vec3d C = facemodel::eye_anchor() + (double)p.offset_x * io * X + (double)p.offset_y * io * Y +
			(double)p.depth * io * N;

	const double hw = 0.5 * io * (double)p.scale_x;
	const double hh = 0.5 * io * (double)p.scale_y;

	// Triangle-strip order: TL, TR, BL, BR. UV (0,0) = top-left of the mask.
	const std::array<Vec3d, 4> corners = {
		C - hw * X + hh * Y, // TL
		C + hw * X + hh * Y, // TR
		C - hw * X - hh * Y, // BL
		C + hw * X - hh * Y, // BR
	};
	const std::array<std::array<double, 2>, 4> uv = {{{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}}};

	std::array<Point2d, 4> screen;
	std::array<double, 4> zc;
	for (int i = 0; i < 4; ++i) {
		project_point(pose, corners[i], screen[i], zc[i]);
		if (zc[i] <= 1e-3)
			return; // corner behind camera -> skip this frame
	}

	// Build a 4-vertex strip: positions in ortho/pixel space, projective uvq.
	struct gs_vb_data *vbd = gs_vbdata_create();
	vbd->num = 4;
	vbd->points = (struct vec3 *)bzalloc(sizeof(struct vec3) * 4);
	vbd->num_tex = 1;
	vbd->tvarray = (struct gs_tvertarray *)bzalloc(sizeof(struct gs_tvertarray));
	// D3D11 only accepts texture-vertex widths of 2 or 4 (GL also takes 3).
	// Use 4 — projective texcoord (u/z, v/z, 0, 1/z), divided by .w in the
	// pixel shader — so the same buffer works on both backends.
	vbd->tvarray[0].width = 4;
	vbd->tvarray[0].array = bzalloc(sizeof(float) * 4 * 4);

	float *tc = (float *)vbd->tvarray[0].array;
	for (int i = 0; i < 4; ++i) {
		vbd->points[i].x = (float)screen[i].x;
		vbd->points[i].y = (float)screen[i].y;
		vbd->points[i].z = 0.f;
		double invz = 1.0 / zc[i];
		tc[i * 4 + 0] = (float)(uv[i][0] * invz);
		tc[i * 4 + 1] = (float)(uv[i][1] * invz);
		tc[i * 4 + 2] = 0.f;
		tc[i * 4 + 3] = (float)invz;
	}

	gs_vertbuffer_t *vb = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
	if (!vb)
		return;

	gs_effect_set_texture(p_image_, tex);
	gs_effect_set_float(p_opacity_, p.opacity);

	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);

	gs_load_vertexbuffer(vb);
	gs_load_indexbuffer(nullptr);

	gs_technique_t *tech = gs_effect_get_technique(effect_, "Draw");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		if (gs_technique_begin_pass(tech, i)) {
			gs_draw(GS_TRISTRIP, 0, 4);
			gs_technique_end_pass(tech);
		}
	}
	gs_technique_end(tech);

	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(vb);
}

gs_indexbuffer_t *MaskRenderer::mesh_index_buffer()
{
	if (mesh_ib_)
		return mesh_ib_;
	const size_t n = (size_t)facemesh::kNumTris * 3;
	uint32_t *idx = (uint32_t *)bmalloc(sizeof(uint32_t) * n);
	for (int t = 0; t < facemesh::kNumTris; ++t) {
		idx[t * 3 + 0] = facemesh::kTris[t][0];
		idx[t * 3 + 1] = facemesh::kTris[t][1];
		idx[t * 3 + 2] = facemesh::kTris[t][2];
	}
	// gs_indexbuffer_create takes ownership of idx (freed on destroy).
	mesh_ib_ = gs_indexbuffer_create(GS_UNSIGNED_LONG, idx, n, 0);
	return mesh_ib_;
}

void MaskRenderer::render_mesh(const std::vector<Point2f> &mesh, float opacity)
{
	if (!effect_ || (int)mesh.size() < facemesh::kNumVerts)
		return;

	gs_texture_t *tex = resolve_mask_texture();
	if (!tex)
		return;

	gs_indexbuffer_t *ib = mesh_index_buffer();
	if (!ib)
		return;

	// Dynamic vertex buffer: deformed mesh positions (screen px) + frontal
	// UV. Texcoord width 2 (plain UV) — valid on D3D11 and GL.
	struct gs_vb_data *vbd = gs_vbdata_create();
	vbd->num = facemesh::kNumVerts;
	vbd->points = (struct vec3 *)bzalloc(sizeof(struct vec3) * facemesh::kNumVerts);
	vbd->num_tex = 1;
	vbd->tvarray = (struct gs_tvertarray *)bzalloc(sizeof(struct gs_tvertarray));
	vbd->tvarray[0].width = 2;
	vbd->tvarray[0].array = bzalloc(sizeof(float) * 2 * facemesh::kNumVerts);

	float *tc = (float *)vbd->tvarray[0].array;
	for (int i = 0; i < facemesh::kNumVerts; ++i) {
		vbd->points[i].x = mesh[i].x;
		vbd->points[i].y = mesh[i].y;
		vbd->points[i].z = 0.f;
		tc[i * 2 + 0] = facemesh::kUV[i][0];
		tc[i * 2 + 1] = facemesh::kUV[i][1];
	}

	gs_vertbuffer_t *vb = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
	if (!vb)
		return;

	gs_effect_set_texture(p_image_, tex);
	gs_effect_set_float(p_opacity_, opacity);

	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);

	gs_load_vertexbuffer(vb);
	gs_load_indexbuffer(ib);

	gs_technique_t *tech = gs_effect_get_technique(effect_, "DrawMesh");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		if (gs_technique_begin_pass(tech, i)) {
			gs_draw(GS_TRIS, 0, 0); // 0 -> all indices
			gs_technique_end_pass(tech);
		}
	}
	gs_technique_end(tech);

	gs_load_indexbuffer(nullptr);
	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(vb);
}

} // namespace FaceMask
