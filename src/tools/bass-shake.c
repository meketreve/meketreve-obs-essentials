/*
Meketreve OBS Essentials - Bass Shake tool
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

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692f

struct bass_shake {
	obs_source_t *context;

	/* graphics */
	gs_effect_t *effect;
	gs_eparam_t *param_offset;

	/* audio source binding */
	char *audio_name;
	obs_source_t *audio_src; /* strong ref while callback attached */
	bool cb_added;

	/* audio analysis (written on audio thread, read on graphics thread) */
	volatile float level; /* smoothed bass RMS, ~0..1 */
	float lp_state;       /* one-pole low-pass state */
	float lp_coef;        /* low-pass coefficient from cutoff + sample rate */

	/* settings */
	float intensity;   /* max displacement in pixels */
	float sensitivity; /* level multiplier before clamping */
	float threshold;   /* noise gate on level */
	float smoothing;   /* seconds toward each random target */

	/* shake state (graphics thread) */
	float off_x, off_y;
	float tgt_x, tgt_y;
};

static inline float rand_bipolar(void)
{
	return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

/* ---- audio capture ---------------------------------------------------- */

static void audio_capture(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	UNUSED_PARAMETER(source);
	struct bass_shake *f = param;

	if (muted || !data || !data->data[0] || data->frames == 0) {
		f->level = 0.0f;
		return;
	}

	const float *samples = (const float *)data->data[0];
	const uint32_t n = data->frames;
	const float a = f->lp_coef;

	float lp = f->lp_state;
	double sum = 0.0;
	for (uint32_t i = 0; i < n; i++) {
		lp += a * (samples[i] - lp);
		sum += (double)lp * (double)lp;
	}
	f->lp_state = lp;

	float rms = (float)sqrt(sum / (double)n);
	/* light attack/decay smoothing so the value is not jumpy */
	f->level = f->level * 0.6f + rms * 0.4f;
}

static void detach_audio(struct bass_shake *f)
{
	if (f->cb_added && f->audio_src)
		obs_source_remove_audio_capture_callback(f->audio_src, audio_capture, f);
	if (f->audio_src) {
		obs_source_release(f->audio_src);
		f->audio_src = NULL;
	}
	f->cb_added = false;
	f->level = 0.0f;
}

/* ---- source callbacks ------------------------------------------------- */

static const char *bass_shake_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("BassShake");
}

static void bass_shake_update(void *data, obs_data_t *settings)
{
	struct bass_shake *f = data;

	f->intensity = (float)obs_data_get_double(settings, "intensity");
	f->sensitivity = (float)obs_data_get_double(settings, "sensitivity");
	f->threshold = (float)obs_data_get_double(settings, "threshold");
	f->smoothing = (float)obs_data_get_double(settings, "smoothing");

	/* one-pole low-pass coefficient: a = dt / (RC + dt) */
	float fc = (float)obs_data_get_double(settings, "bass_cutoff");
	if (fc < 1.0f)
		fc = 1.0f;
	struct obs_audio_info oai;
	float fs = 48000.0f;
	if (obs_get_audio_info(&oai))
		fs = (float)oai.samples_per_sec;
	float dt = 1.0f / fs;
	float rc = 1.0f / (TWO_PI * fc);
	f->lp_coef = dt / (rc + dt);

	/* (re)bind audio source */
	const char *name = obs_data_get_string(settings, "audio_source");
	bool changed = (f->audio_name == NULL) || (strcmp(f->audio_name, name) != 0);
	if (changed) {
		detach_audio(f);
		bfree(f->audio_name);
		f->audio_name = bstrdup(name);
		if (name && *name) {
			obs_source_t *src = obs_get_source_by_name(name);
			if (src) {
				obs_source_add_audio_capture_callback(src, audio_capture, f);
				f->audio_src = src; /* keep ref */
				f->cb_added = true;
			}
		}
	}
}

static void *bass_shake_create(obs_data_t *settings, obs_source_t *context)
{
	static bool seeded = false;
	if (!seeded) {
		srand((unsigned int)os_gettime_ns());
		seeded = true;
	}

	struct bass_shake *f = bzalloc(sizeof(struct bass_shake));
	f->context = context;

	obs_enter_graphics();
	char *path = obs_module_file("effects/bass-shake.effect");
	char *error = NULL;
	f->effect = gs_effect_create_from_file(path, &error);
	bfree(path);
	if (f->effect)
		f->param_offset = gs_effect_get_param_by_name(f->effect, "uv_offset");
	else
		obs_log(LOG_ERROR, "[bass-shake] failed to load effect: %s", error ? error : "(unknown)");
	bfree(error);
	obs_leave_graphics();

	bass_shake_update(f, settings);
	return f;
}

static void bass_shake_destroy(void *data)
{
	struct bass_shake *f = data;
	detach_audio(f);
	if (f->effect) {
		obs_enter_graphics();
		gs_effect_destroy(f->effect);
		obs_leave_graphics();
	}
	bfree(f->audio_name);
	bfree(f);
}

static void bass_shake_tick(void *data, float seconds)
{
	struct bass_shake *f = data;

	float lvl = f->level;
	if (lvl < f->threshold)
		lvl = 0.0f;

	float norm = lvl * f->sensitivity;
	if (norm > 1.0f)
		norm = 1.0f;
	float amp = norm * f->intensity;

	/* pick a fresh random target each tick, then ease toward it */
	f->tgt_x = rand_bipolar() * amp;
	f->tgt_y = rand_bipolar() * amp;

	float t = (f->smoothing <= 0.0f) ? 1.0f : (1.0f - expf(-seconds / f->smoothing));
	f->off_x += (f->tgt_x - f->off_x) * t;
	f->off_y += (f->tgt_y - f->off_y) * t;
}

static void bass_shake_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct bass_shake *f = data;

	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t cx = target ? obs_source_get_base_width(target) : 0;
	uint32_t cy = target ? obs_source_get_base_height(target) : 0;

	if (cx == 0 || cy == 0 || !f->effect) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	struct vec2 offset;
	offset.x = f->off_x / (float)cx;
	offset.y = f->off_y / (float)cy;
	gs_effect_set_vec2(f->param_offset, &offset);

	obs_source_process_filter_end(f->context, f->effect, cx, cy);
}

static void bass_shake_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "intensity", 40.0);
	obs_data_set_default_double(settings, "sensitivity", 12.0);
	obs_data_set_default_double(settings, "threshold", 0.02);
	obs_data_set_default_double(settings, "smoothing", 0.04);
	obs_data_set_default_double(settings, "bass_cutoff", 150.0);
}

static bool enum_audio_sources(void *param, obs_source_t *source)
{
	uint32_t flags = obs_source_get_output_flags(source);
	if (flags & OBS_SOURCE_AUDIO) {
		obs_property_t *list = param;
		const char *name = obs_source_get_name(source);
		obs_property_list_add_string(list, name, name);
	}
	return true;
}

static obs_properties_t *bass_shake_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *list = obs_properties_add_list(props, "audio_source", obs_module_text("BassShake.AudioSource"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, obs_module_text("BassShake.None"), "");
	obs_enum_sources(enum_audio_sources, list);

	obs_properties_add_float_slider(props, "intensity", obs_module_text("BassShake.Intensity"), 0.0, 300.0, 1.0);
	obs_properties_add_float_slider(props, "sensitivity", obs_module_text("BassShake.Sensitivity"), 1.0, 50.0, 0.5);
	obs_properties_add_float_slider(props, "threshold", obs_module_text("BassShake.Threshold"), 0.0, 0.5, 0.001);
	obs_properties_add_float_slider(props, "smoothing", obs_module_text("BassShake.Smoothing"), 0.0, 0.5, 0.005);
	obs_properties_add_float_slider(props, "bass_cutoff", obs_module_text("BassShake.BassCutoff"), 20.0, 500.0, 5.0);

	return props;
}

static struct obs_source_info bass_shake_info = {
	.id = "meketreve_bass_shake",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = bass_shake_get_name,
	.create = bass_shake_create,
	.destroy = bass_shake_destroy,
	.update = bass_shake_update,
	.video_tick = bass_shake_tick,
	.video_render = bass_shake_render,
	.get_defaults = bass_shake_defaults,
	.get_properties = bass_shake_properties,
};

void bass_shake_register(void)
{
	obs_register_source(&bass_shake_info);
}
