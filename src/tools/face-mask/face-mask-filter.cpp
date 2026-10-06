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
#include "face-mask.h"
#include "frame-grab.hpp"
#include "tracker.hpp"
#include "pose.hpp"
#include "mask-renderer.hpp"
#include "smoothing.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

/* Face mask: a video filter that tracks the face on its source and draws an
 * image or video (another OBS source) over it, as a quad that turns with the
 * head or deformed onto the 468-point face mesh. Detection runs on a worker
 * thread (FaceMask::FaceTracker); the graphics thread only grabs a downscaled
 * frame now and then and draws. */

namespace {

using namespace FaceMask;

/* The models live in the plugin's config folder, where they are downloaded. */
constexpr const char *kModelFile = "face_detection_yunet_2023mar.onnx";
constexpr const char *kHeadposeFile = "headpose_mobilenetv2.onnx";
constexpr const char *kLandmarkFile = "face_landmark_468.onnx";

std::string modelPath(const char *file)
{
	char *path = obs_module_config_path((std::string("face-mask/models/") + file).c_str());
	std::string found = path && os_file_exists(path) ? path : "";
	bfree(path);
	return found;
}

struct mask_filter {
	obs_source_t *source = nullptr;

	FrameGrabber grabber;
	FaceTracker tracker;
	MaskRenderer renderer;
	PoseSmoother smoother;

	// settings
	bool debug = false;
	bool flip_readback = false;
	int detect_max_dim = 384;
	float score_thresh = 0.6f;
	double fov_deg = 60.0;
	double rotation_follow = 1.0;
	double hold_ms = 200.0;
	double fade_ms = 300.0;
	double smoothing = 0.5;
	bool mesh_mode = false;
	bool invert_pitch = false;
	bool invert_yaw = false;
	bool invert_roll = false;
	uint64_t detect_interval_ns = 33'000'000; // ~30 fps
	MaskParams params;

	// runtime
	uint64_t last_grab_ns = 0;
	uint64_t frame_counter = 0;
	bool effect_tried = false;

	// face-loss handling
	HeadPose last_good_pose;
	bool have_last_good = false;
	uint64_t last_valid_ns = 0;

	// mesh-morph mode: EMA-smoothed landmark positions
	std::vector<Point2f> mesh_s;
	bool mesh_init = false;
};

/* ---- property keys ---- */
constexpr const char *kMaskSource = "mask_source";
constexpr const char *kScaleX = "scale_x";
constexpr const char *kScaleY = "scale_y";
constexpr const char *kOffX = "offset_x";
constexpr const char *kOffY = "offset_y";
constexpr const char *kDepth = "depth";
constexpr const char *kOpacity = "opacity";
constexpr const char *kFov = "fov";
constexpr const char *kRotFollow = "rotation_follow";
constexpr const char *kSmoothing = "smoothing";
constexpr const char *kAvgWindow = "avg_window";
constexpr const char *kHoldMs = "hold_ms";
constexpr const char *kFadeMs = "fade_ms";
constexpr const char *kInvPitch = "invert_pitch";
constexpr const char *kInvYaw = "invert_yaw";
constexpr const char *kInvRoll = "invert_roll";
constexpr const char *kDebug = "debug";
constexpr const char *kUseMesh = "use_mesh";
constexpr const char *kMeshMode = "mesh_mode";

// Re-acquire after a gap longer than this -> reset smoother (avoid slerp jump).
constexpr uint64_t kReacquireResetNs = 200'000'000; // 200 ms
constexpr const char *kFlip = "flip_readback";
constexpr const char *kMaxDim = "detect_max_dim";
constexpr const char *kFps = "detect_fps";
constexpr const char *kScore = "score_thresh";

/* ---- small solid-color draw helper (debug overlay) ---- */
void draw_solid_rect(float x, float y, float w, float h, const vec4 &color)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *col = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_vec4(col, &color);

	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		if (!gs_technique_begin_pass(tech, i))
			continue;
		gs_matrix_push();
		gs_matrix_translate3f(x, y, 0.f);
		gs_draw_sprite(nullptr, 0, (uint32_t)std::max(1.f, w), (uint32_t)std::max(1.f, h));
		gs_matrix_pop();
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
}

void draw_dot(const Point2f &p, float s, const vec4 &color)
{
	draw_solid_rect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, color);
}

void draw_box(const Rect2f &b, float t, const vec4 &color)
{
	draw_solid_rect(b.x, b.y, b.width, t, color);                // top
	draw_solid_rect(b.x, b.y + b.height - t, b.width, t, color); // bottom
	draw_solid_rect(b.x, b.y, t, b.height, color);               // left
	draw_solid_rect(b.x + b.width - t, b.y, t, b.height, color); // right
}

/* ---- obs_source_info callbacks ---- */

const char *mask_get_name(void *)
{
	return obs_module_text("FaceMask.Name");
}

void mask_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<mask_filter *>(data);

	f->renderer.set_source(obs_data_get_string(settings, kMaskSource));

	f->params.scale_x = (float)obs_data_get_double(settings, kScaleX);
	f->params.scale_y = (float)obs_data_get_double(settings, kScaleY);
	f->params.offset_x = (float)obs_data_get_double(settings, kOffX);
	f->params.offset_y = (float)obs_data_get_double(settings, kOffY);
	f->params.depth = (float)obs_data_get_double(settings, kDepth);
	f->params.opacity = (float)obs_data_get_double(settings, kOpacity);
	f->fov_deg = obs_data_get_double(settings, kFov);
	f->rotation_follow = obs_data_get_double(settings, kRotFollow);
	f->params.rotation_follow = (float)f->rotation_follow;
	f->smoothing = obs_data_get_double(settings, kSmoothing);
	f->smoother.set_smoothing(f->smoothing);
	f->smoother.set_avg_window((int)obs_data_get_int(settings, kAvgWindow));
	f->mesh_mode = obs_data_get_bool(settings, kMeshMode);
	f->hold_ms = obs_data_get_double(settings, kHoldMs);
	f->fade_ms = obs_data_get_double(settings, kFadeMs);
	f->invert_pitch = obs_data_get_bool(settings, kInvPitch);
	f->invert_yaw = obs_data_get_bool(settings, kInvYaw);
	f->invert_roll = obs_data_get_bool(settings, kInvRoll);

	f->debug = obs_data_get_bool(settings, kDebug);
	f->flip_readback = obs_data_get_bool(settings, kFlip);
	f->detect_max_dim = (int)obs_data_get_int(settings, kMaxDim);
	f->score_thresh = (float)obs_data_get_double(settings, kScore);

	int fps = (int)obs_data_get_int(settings, kFps);
	if (fps < 1)
		fps = 1;
	f->detect_interval_ns = (uint64_t)(1'000'000'000ull / (uint64_t)fps);

	f->tracker.set_score_threshold(f->score_thresh);
	f->tracker.set_landmarks_enabled(obs_data_get_bool(settings, kUseMesh));
}

void *mask_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new mask_filter();
	f->source = source;

	mask_update(f, settings);

	const std::string yunet = modelPath(kModelFile);
	const std::string headpose = modelPath(kHeadposeFile);
	if (!yunet.empty() && !headpose.empty())
		f->tracker.start(yunet, headpose, modelPath(kLandmarkFile), f->score_thresh);
	else
		obs_log(LOG_WARNING, "[face-mask] models not found in face-mask/models: %s / %s", kModelFile,
			kHeadposeFile);
	return f;
}

void mask_destroy(void *data)
{
	auto *f = static_cast<mask_filter *>(data);
	f->tracker.stop();
	obs_enter_graphics();
	f->grabber.release();
	f->renderer.release();
	obs_leave_graphics();
	delete f;
}

void mask_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kMaskSource, "");
	obs_data_set_default_double(settings, kScaleX, 2.2);
	obs_data_set_default_double(settings, kScaleY, 1.2);
	obs_data_set_default_double(settings, kOffX, 0.0);
	obs_data_set_default_double(settings, kOffY, 0.0);
	obs_data_set_default_double(settings, kDepth, 0.3);
	obs_data_set_default_double(settings, kOpacity, 1.0);
	obs_data_set_default_double(settings, kFov, 60.0);
	obs_data_set_default_double(settings, kRotFollow, 1.0);
	obs_data_set_default_double(settings, kSmoothing, 0.5);
	obs_data_set_default_int(settings, kAvgWindow, 1);
	obs_data_set_default_double(settings, kHoldMs, 200.0);
	obs_data_set_default_double(settings, kFadeMs, 300.0);
	obs_data_set_default_bool(settings, kInvPitch, false);
	obs_data_set_default_bool(settings, kInvYaw, false);
	obs_data_set_default_bool(settings, kInvRoll, false);

	obs_data_set_default_bool(settings, kUseMesh, true);
	obs_data_set_default_bool(settings, kMeshMode, false);
	obs_data_set_default_bool(settings, kDebug, false);
	obs_data_set_default_bool(settings, kFlip, false);
	obs_data_set_default_int(settings, kMaxDim, 384);
	obs_data_set_default_int(settings, kFps, 30);
	obs_data_set_default_double(settings, kScore, 0.6);
}

bool add_video_source_to_list(void *data, obs_source_t *src)
{
	auto *list = static_cast<obs_property_t *>(data);
	if (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO) {
		const char *name = obs_source_get_name(src);
		if (name && *name)
			obs_property_list_add_string(list, name, name);
	}
	return true;
}

obs_properties_t *mask_properties(void *)
{
	obs_properties_t *p = obs_properties_create();

	obs_property_t *ml = obs_properties_add_list(p, kMaskSource, obs_module_text("FaceMask.Source"),
						     OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(ml, obs_module_text("FaceMask.None"), "");
	obs_enum_sources(add_video_source_to_list, ml);

	obs_properties_add_float_slider(p, kScaleX, obs_module_text("FaceMask.ScaleX"), 0.2, 6.0, 0.05);
	obs_properties_add_float_slider(p, kScaleY, obs_module_text("FaceMask.ScaleY"), 0.2, 6.0, 0.05);
	obs_properties_add_float_slider(p, kOffX, obs_module_text("FaceMask.OffsetX"), -2.0, 2.0, 0.02);
	obs_properties_add_float_slider(p, kOffY, obs_module_text("FaceMask.OffsetY"), -2.0, 2.0, 0.02);
	obs_properties_add_float_slider(p, kDepth, obs_module_text("FaceMask.Depth"), -2.0, 2.0, 0.02);
	obs_properties_add_float_slider(p, kOpacity, obs_module_text("FaceMask.Opacity"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, kFov, obs_module_text("FaceMask.Fov"), 20.0, 120.0, 1.0);
	obs_properties_add_float_slider(p, kRotFollow, obs_module_text("FaceMask.RotationFollow"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, kSmoothing, obs_module_text("FaceMask.Smoothing"), 0.0, 1.0, 0.01);
	obs_properties_add_int_slider(p, kAvgWindow, obs_module_text("FaceMask.AverageWindow"), 1, 30, 1);
	obs_properties_add_float_slider(p, kHoldMs, obs_module_text("FaceMask.HoldMs"), 0.0, 2000.0, 10.0);
	obs_properties_add_float_slider(p, kFadeMs, obs_module_text("FaceMask.FadeMs"), 0.0, 2000.0, 10.0);

	obs_properties_add_bool(p, kInvPitch, obs_module_text("FaceMask.InvertPitch"));
	obs_properties_add_bool(p, kInvYaw, obs_module_text("FaceMask.InvertYaw"));
	obs_properties_add_bool(p, kInvRoll, obs_module_text("FaceMask.InvertRoll"));

	obs_properties_add_bool(p, kUseMesh, obs_module_text("FaceMask.UseMesh"));
	obs_properties_add_bool(p, kMeshMode, obs_module_text("FaceMask.MeshMode"));
	obs_properties_add_bool(p, kDebug, obs_module_text("FaceMask.DebugOverlay"));
	obs_properties_add_bool(p, kFlip, obs_module_text("FaceMask.DebugFlip"));
	obs_properties_add_int_slider(p, kMaxDim, obs_module_text("FaceMask.DetectMaxDim"), 160, 960, 16);
	obs_properties_add_int_slider(p, kFps, obs_module_text("FaceMask.DetectFps"), 1, 60, 1);
	obs_properties_add_float_slider(p, kScore, obs_module_text("FaceMask.DetectScore"), 0.1, 0.99, 0.01);
	return p;
}

void mask_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *f = static_cast<mask_filter *>(data);

	obs_source_t *target = obs_filter_get_target(f->source);
	if (!target) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(f->source);
		return;
	}

	// 1. Throttled offscreen grab -> submit to tracker (before drawing).
	if (f->tracker.running()) {
		uint64_t now = os_gettime_ns();
		if (now - f->last_grab_ns >= f->detect_interval_ns) {
			Image bgr;
			float scale = 1.f;
			if (f->grabber.grab(target, w, h, f->detect_max_dim, f->flip_readback, bgr, scale)) {
				f->tracker.submit(std::move(bgr), scale, ++f->frame_counter);
			}
			f->last_grab_ns = now;
		}
	}

	// 2. Visible passthrough.
	obs_source_skip_video_filter(f->source);

	FaceResult r = f->tracker.latest();

	// 3. Mask: solve head pose, smooth, and draw the 3D-projected quad.
	//    On face loss: hold the last good pose, then fade out the opacity.
	if (!f->effect_tried) {
		f->renderer.load_effect();
		f->effect_tried = true;
	}
	if (f->renderer.effect_loaded()) {
		try {
			if (f->mesh_mode && r.has_mesh) {
				// Mesh-morph: deform the mask onto the live FaceMesh.
				// EMA-smooth the raw landmark points (slider-controlled).
				if (!f->mesh_init || f->mesh_s.size() != r.mesh.size()) {
					f->mesh_s = r.mesh;
					f->mesh_init = true;
				} else {
					double a = std::clamp(1.0 - f->smoothing * 0.9, 0.1, 1.0);
					for (size_t i = 0; i < r.mesh.size(); ++i) {
						f->mesh_s[i].x = (float)(a * r.mesh[i].x + (1.0 - a) * f->mesh_s[i].x);
						f->mesh_s[i].y = (float)(a * r.mesh[i].y + (1.0 - a) * f->mesh_s[i].y);
					}
				}
				f->renderer.render_mesh(f->mesh_s, f->params.opacity);
			} else {
				f->mesh_init = false; // reset so re-entry doesn't jump
				uint64_t now = os_gettime_ns();
				HeadPose draw_pose;
				bool draw = false;
				float fade = 1.0f;

				HeadPose raw;
				if (r.valid && build_pose_from_net(r, (int)w, (int)h, f->fov_deg, f->invert_pitch,
								   f->invert_yaw, f->invert_roll, raw)) {
					// Re-acquired after a long gap -> reset to avoid a slerp jump.
					if (f->have_last_good && (now - f->last_valid_ns) > kReacquireResetNs)
						f->smoother.reset();

					draw_pose = f->smoother.smooth(raw, now, f->rotation_follow);
					f->last_good_pose = draw_pose;
					f->have_last_good = true;
					f->last_valid_ns = now;
					draw = true;
				} else if (f->have_last_good) {
					double since_ms = (double)(now - f->last_valid_ns) * 1e-6;
					if (since_ms <= f->hold_ms) {
						draw_pose = f->last_good_pose;
						draw = true;
					} else if (since_ms <= f->hold_ms + f->fade_ms && f->fade_ms > 0.0) {
						draw_pose = f->last_good_pose;
						fade = (float)(1.0 - (since_ms - f->hold_ms) / f->fade_ms);
						draw = true;
					} else {
						f->have_last_good = false; // fully gone
					}
				}

				if (draw) {
					MaskParams p = f->params;
					p.opacity *= fade;
					f->renderer.render(draw_pose, p);
				}
			}
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "[face-mask] render error: %s", e.what());
		}
	}

	// 4. Debug overlay: draw detected landmarks in source pixel space.
	if (f->debug) {
		if (r.valid) {
			float s = std::max(4.f, (float)std::max(w, h) / 100.f);
			vec4 green, red, blue, yellow;
			vec4_set(&green, 0.f, 1.f, 0.f, 1.f);
			vec4_set(&red, 1.f, 0.2f, 0.2f, 1.f);
			vec4_set(&blue, 0.3f, 0.5f, 1.f, 1.f);
			vec4_set(&yellow, 1.f, 1.f, 0.f, 1.f);

			// Dense FaceMesh points (small, green) when available.
			if (r.has_mesh) {
				float ms = std::max(1.5f, s * 0.18f);
				for (const auto &pt : r.mesh)
					draw_dot(pt, ms, green);
			}

			draw_box(r.bbox, std::max(2.f, s * 0.25f), green);
			draw_dot(r.right_eye, s, red);
			draw_dot(r.left_eye, s, red);
			draw_dot(r.nose, s, yellow);
			draw_dot(r.mouth_right, s, blue);
			draw_dot(r.mouth_left, s, blue);
		}
	}
}

} // namespace

void face_mask_register(void)
{
	struct obs_source_info info = {};
	info.id = "meketreve_face_mask";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = mask_get_name;
	info.create = mask_create;
	info.destroy = mask_destroy;
	info.get_defaults = mask_defaults;
	info.get_properties = mask_properties;
	info.update = mask_update;
	info.video_render = mask_video_render;
	obs_register_source(&info);
}
