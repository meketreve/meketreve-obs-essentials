/*
Meketreve OBS Essentials - PNGTuber
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
#include "pngtuber.h"
#include "pngtuber-logic.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/image-file.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <random>
#include <string>

/* An avatar made of up to four pictures: mouth closed / open, each with
 * eyes open / closed. It opens the mouth while a chosen audio source is
 * loud enough, blinks by itself and hops when it starts talking. Only the
 * first picture is needed; GIFs animate. */

namespace {

enum Picture { Idle, Talk, IdleBlink, TalkBlink, PictureCount };
const char *const kPictureKeys[PictureCount] = {"image_idle", "image_talk", "image_idle_blink", "image_talk_blink"};
const char *const kPictureLabels[PictureCount] = {"PngTuber.ImageIdle", "PngTuber.ImageTalk", "PngTuber.ImageIdleBlink",
						  "PngTuber.ImageTalkBlink"};
const char *const kExampleFiles[PictureCount] = {"pngtuber/texugo-idle.png", "pngtuber/texugo-talk.png",
						 "pngtuber/texugo-idle-blink.png", "pngtuber/texugo-talk-blink.png"};
constexpr uint64_t kRebindNs = 1000000000ull; /* look for the audio source again every second */

struct pngtuber {
	obs_source_t *source = nullptr;
	gs_image_file4_t images[PictureCount] = {};
	std::string paths[PictureCount];
	PngTuber::Avatar avatar;

	std::string audioName;
	obs_weak_source_t *audio = nullptr; /* the source the callback is on */
	std::atomic<float> levelDb{-120.f};
	uint64_t lastBind = 0;

	explicit pngtuber(std::function<double()> rand01) : avatar(std::move(rand01)) {}
};

std::mt19937 &rng()
{
	static std::mt19937 gen{std::random_device{}()};
	return gen;
}

const char *pngtuber_get_name(void *)
{
	return obs_module_text("PngTuber.Name");
}

/* ---- audio ---- */

void audio_callback(void *param, obs_source_t *, const struct audio_data *data, bool muted)
{
	auto *p = static_cast<pngtuber *>(param);
	if (muted || !data || data->frames == 0) {
		p->levelDb.store(-120.f);
		return;
	}
	/* The loudest channel. */
	double db = -120.0;
	for (size_t ch = 0; ch < MAX_AV_PLANES; ++ch) {
		if (data->data[ch])
			db = std::max(db, PngTuber::Avatar::levelDb(reinterpret_cast<const float *>(data->data[ch]),
								    data->frames));
	}
	p->levelDb.store(static_cast<float>(db));
}

void detach_audio(pngtuber *p)
{
	if (p->audio) {
		if (obs_source_t *src = obs_weak_source_get_source(p->audio)) {
			obs_source_remove_audio_capture_callback(src, audio_callback, p);
			obs_source_release(src);
		}
		obs_weak_source_release(p->audio);
		p->audio = nullptr;
	}
	p->levelDb.store(-120.f);
}

/* The audio source may load after this one, or be renamed back: tried
 * again from the tick until it is found. None chosen = OBS's own
 * microphone (Mic/Aux, the first of output channels 3 to 5). */
void attach_audio(pngtuber *p)
{
	if (p->audio)
		return;
	obs_source_t *src = nullptr;
	if (!p->audioName.empty()) {
		src = obs_get_source_by_name(p->audioName.c_str());
	} else {
		for (uint32_t channel = 3; channel <= 5 && !src; ++channel)
			src = obs_get_output_source(channel);
	}
	if (!src)
		return;
	obs_log(LOG_INFO, "[pngtuber] listening to %s", obs_source_get_name(src));
	obs_source_add_audio_capture_callback(src, audio_callback, p);
	p->audio = obs_source_get_weak_source(src);
	obs_source_release(src);
}

/* ---- pictures ---- */

void free_pictures(gs_image_file4_t (&images)[PictureCount])
{
	obs_enter_graphics();
	for (gs_image_file4_t &img : images)
		gs_image_file4_free(&img);
	obs_leave_graphics();
}

gs_image_file4_t *picture(pngtuber *p, Picture which)
{
	gs_image_file4_t *img = &p->images[which];
	return img->image3.image2.image.texture ? img : nullptr;
}

/* The picture for the current state, falling back to the plainer one. */
gs_image_file4_t *current(pngtuber *p)
{
	const bool talk = p->avatar.talking();
	const bool blink = p->avatar.eyesClosed();
	gs_image_file4_t *img = nullptr;
	if (talk && blink)
		img = picture(p, TalkBlink);
	if (!img && talk)
		img = picture(p, Talk);
	if (!img && blink)
		img = picture(p, IdleBlink);
	if (!img)
		img = picture(p, Idle);
	return img;
}

uint32_t base_width(pngtuber *p)
{
	uint32_t w = 0;
	for (int i = 0; i < PictureCount; ++i) {
		if (picture(p, static_cast<Picture>(i)))
			w = std::max(w, p->images[i].image3.image2.image.cx);
	}
	return w;
}

uint32_t base_height(pngtuber *p)
{
	uint32_t h = 0;
	for (int i = 0; i < PictureCount; ++i) {
		if (picture(p, static_cast<Picture>(i)))
			h = std::max(h, p->images[i].image3.image2.image.cy);
	}
	return h;
}

/* ---- source callbacks ---- */

void pngtuber_update(void *data, obs_data_t *settings)
{
	auto *p = static_cast<pngtuber *>(data);

	PngTuber::Settings s;
	s.thresholdDb = obs_data_get_double(settings, "threshold_db");
	s.holdMs = obs_data_get_double(settings, "hold_ms");
	s.blink = obs_data_get_bool(settings, "blink");
	s.blinkMinS = obs_data_get_double(settings, "blink_min_s");
	s.blinkMaxS = obs_data_get_double(settings, "blink_max_s");
	s.blinkMs = obs_data_get_double(settings, "blink_ms");
	s.hopPx = obs_data_get_double(settings, "hop_px");
	s.hopMs = obs_data_get_double(settings, "hop_ms");
	s.bobPx = obs_data_get_double(settings, "bob_px");
	p->avatar.setSettings(s);

	/* Pictures: decoded here, swapped in under the graphics lock. With
	 * none chosen, the example badger stands in (all four of it). */
	std::string paths[PictureCount];
	bool chosen = false;
	for (int i = 0; i < PictureCount; ++i) {
		paths[i] = obs_data_get_string(settings, kPictureKeys[i]);
		chosen |= !paths[i].empty();
	}
	if (!chosen) {
		for (int i = 0; i < PictureCount; ++i) {
			char *file = obs_module_file(kExampleFiles[i]);
			paths[i] = file ? file : "";
			bfree(file);
		}
	}
	bool changed = false;
	for (int i = 0; i < PictureCount; ++i)
		changed |= paths[i] != p->paths[i];
	if (changed) {
		gs_image_file4_t fresh[PictureCount] = {};
		for (int i = 0; i < PictureCount; ++i) {
			if (!paths[i].empty())
				gs_image_file4_init(&fresh[i], paths[i].c_str(), GS_IMAGE_ALPHA_PREMULTIPLY_SRGB);
		}
		obs_enter_graphics();
		for (int i = 0; i < PictureCount; ++i) {
			gs_image_file4_free(&p->images[i]);
			p->images[i] = fresh[i];
			gs_image_file4_init_texture(&p->images[i]);
			if (!paths[i].empty() && !p->images[i].image3.image2.image.texture)
				obs_log(LOG_WARNING, "[pngtuber] could not load %s", paths[i].c_str());
			p->paths[i] = paths[i];
		}
		obs_leave_graphics();
	}

	const std::string audio = obs_data_get_string(settings, "audio_source");
	if (audio != p->audioName) {
		detach_audio(p);
		p->audioName = audio;
		attach_audio(p);
	}
}

void *pngtuber_create(obs_data_t *settings, obs_source_t *source)
{
	auto *p = new pngtuber([]() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); });
	p->source = source;
	pngtuber_update(p, settings);
	return p;
}

void pngtuber_destroy(void *data)
{
	auto *p = static_cast<pngtuber *>(data);
	detach_audio(p);
	free_pictures(p->images);
	delete p;
}

void pngtuber_defaults(obs_data_t *settings)
{
	PngTuber::Settings s;
	obs_data_set_default_double(settings, "threshold_db", s.thresholdDb);
	obs_data_set_default_double(settings, "hold_ms", s.holdMs);
	obs_data_set_default_bool(settings, "blink", s.blink);
	obs_data_set_default_double(settings, "blink_min_s", s.blinkMinS);
	obs_data_set_default_double(settings, "blink_max_s", s.blinkMaxS);
	obs_data_set_default_double(settings, "blink_ms", s.blinkMs);
	obs_data_set_default_double(settings, "hop_px", s.hopPx);
	obs_data_set_default_double(settings, "hop_ms", s.hopMs);
	obs_data_set_default_double(settings, "bob_px", s.bobPx);
}

bool add_audio_source(void *data, obs_source_t *src)
{
	if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) {
		const char *name = obs_source_get_name(src);
		if (name && *name)
			obs_property_list_add_string(static_cast<obs_property_t *>(data), name, name);
	}
	return true;
}

obs_properties_t *pngtuber_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_text(props, "help", obs_module_text("PngTuber.Help"), OBS_TEXT_INFO);

	const std::string filter = std::string(obs_module_text("PngTuber.ImageFilter")) +
				   " (*.png *.gif *.jpg *.jpeg *.webp *.bmp);;" + obs_module_text("PngTuber.AllFiles") +
				   " (*.*)";
	for (int i = 0; i < PictureCount; ++i)
		obs_properties_add_path(props, kPictureKeys[i], obs_module_text(kPictureLabels[i]), OBS_PATH_FILE,
					filter.c_str(), nullptr);

	obs_property_t *audio = obs_properties_add_list(props, "audio_source", obs_module_text("PngTuber.Audio"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(audio, obs_module_text("PngTuber.AudioNone"), "");
	obs_enum_sources(add_audio_source, audio);

	obs_property_t *threshold = obs_properties_add_float_slider(
		props, "threshold_db", obs_module_text("PngTuber.Threshold"), -80.0, 0.0, 1.0);
	obs_property_float_set_suffix(threshold, " dB");
	obs_property_t *hold =
		obs_properties_add_float_slider(props, "hold_ms", obs_module_text("PngTuber.Hold"), 0.0, 1000.0, 10.0);
	obs_property_float_set_suffix(hold, " ms");

	obs_properties_add_bool(props, "blink", obs_module_text("PngTuber.Blink"));
	obs_property_t *bmin = obs_properties_add_float_slider(props, "blink_min_s",
							       obs_module_text("PngTuber.BlinkMin"), 0.5, 20.0, 0.5);
	obs_property_float_set_suffix(bmin, " s");
	obs_property_t *bmax = obs_properties_add_float_slider(props, "blink_max_s",
							       obs_module_text("PngTuber.BlinkMax"), 0.5, 20.0, 0.5);
	obs_property_float_set_suffix(bmax, " s");
	obs_property_t *bms = obs_properties_add_float_slider(props, "blink_ms", obs_module_text("PngTuber.BlinkMs"),
							      40.0, 600.0, 10.0);
	obs_property_float_set_suffix(bms, " ms");

	obs_property_t *hop =
		obs_properties_add_float_slider(props, "hop_px", obs_module_text("PngTuber.Hop"), 0.0, 80.0, 1.0);
	obs_property_float_set_suffix(hop, " px");
	obs_property_t *hopMs =
		obs_properties_add_float_slider(props, "hop_ms", obs_module_text("PngTuber.HopMs"), 60.0, 800.0, 10.0);
	obs_property_float_set_suffix(hopMs, " ms");
	obs_property_t *bob =
		obs_properties_add_float_slider(props, "bob_px", obs_module_text("PngTuber.Bob"), 0.0, 40.0, 1.0);
	obs_property_float_set_suffix(bob, " px");
	return props;
}

void pngtuber_tick(void *data, float seconds)
{
	auto *p = static_cast<pngtuber *>(data);

	const uint64_t now = obs_get_video_frame_time();
	if (!p->audio && now - p->lastBind > kRebindNs) {
		p->lastBind = now;
		attach_audio(p);
	}

	p->avatar.setLevel(p->levelDb.load());
	p->avatar.tick(seconds);

	/* Animated pictures. */
	const auto ns = static_cast<uint64_t>(static_cast<double>(seconds) * 1e9);
	bool dirty[PictureCount] = {};
	bool any = false;
	for (int i = 0; i < PictureCount; ++i) {
		if (p->images[i].image3.image2.image.is_animated_gif) {
			dirty[i] = gs_image_file4_tick(&p->images[i], ns);
			any |= dirty[i];
		}
	}
	if (any) {
		obs_enter_graphics();
		for (int i = 0; i < PictureCount; ++i) {
			if (dirty[i])
				gs_image_file4_update_texture(&p->images[i]);
		}
		obs_leave_graphics();
	}
}

uint32_t pngtuber_width(void *data)
{
	return base_width(static_cast<pngtuber *>(data));
}

uint32_t pngtuber_height(void *data)
{
	auto *p = static_cast<pngtuber *>(data);
	const uint32_t h = base_height(p);
	/* Room above for the hop, so it never gets cut. */
	return h ? h + static_cast<uint32_t>(std::ceil(p->avatar.maxLift())) : 0;
}

void pngtuber_render(void *data, gs_effect_t *effect)
{
	auto *p = static_cast<pngtuber *>(data);
	gs_image_file4_t *img = current(p);
	if (!img)
		return;
	gs_texture_t *tex = img->image3.image2.image.texture;
	const uint32_t cx = img->image3.image2.image.cx;
	const uint32_t cy = img->image3.image2.image.cy;
	/* Bottom-centred, lifted by the hop. */
	const float x = (static_cast<float>(pngtuber_width(p)) - static_cast<float>(cx)) * 0.5f;
	const float y =
		static_cast<float>(pngtuber_height(p)) - static_cast<float>(cy) - static_cast<float>(p->avatar.lift());

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), tex);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.f);
	gs_draw_sprite(tex, 0, cx, cy);
	gs_matrix_pop();
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous);
}

} // namespace

void pngtuber_register(void)
{
	struct obs_source_info info = {};
	info.id = "meketreve_pngtuber";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.icon_type = OBS_ICON_TYPE_IMAGE;
	info.get_name = pngtuber_get_name;
	info.create = pngtuber_create;
	info.destroy = pngtuber_destroy;
	info.update = pngtuber_update;
	info.get_defaults = pngtuber_defaults;
	info.get_properties = pngtuber_properties;
	info.video_tick = pngtuber_tick;
	info.video_render = pngtuber_render;
	info.get_width = pngtuber_width;
	info.get_height = pngtuber_height;
	obs_register_source(&info);
}
