/*
Meketreve OBS Essentials - Voice FX Mixer tool
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

#include <math.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f
#define TWO_PI_F 6.28318530717958647692f
#define GRAIN 1024 /* pitch shifter grain size in samples */

/* A single biquad filter (Direct Form I) state. */
struct biquad {
	float x1, x2, y1, y2;
};

/* Per-channel DSP state. */
struct fx_chan {
	/* pitch shifter (two-tap crossfade granular) */
	float pbuf[GRAIN];
	uint32_t pw; /* write index */
	float pr;    /* fractional read index */

	/* telephone band-pass: high-pass then low-pass biquad */
	struct biquad hp;
	struct biquad lp;

	/* bitcrush sample-and-hold */
	int crush_cnt;
	float crush_hold;

	/* echo delay line (allocated at create) */
	float *echo;
	uint32_t echo_w;
};

struct voice_fx {
	obs_source_t *context;

	uint32_t sample_rate;
	uint64_t t; /* running sample counter for LFO/carrier phase */

	struct fx_chan ch[MAX_AV_PLANES];
	uint32_t echo_len; /* samples allocated per channel echo line */

	/* gains */
	float in_gain, out_gain;

	/* pitch */
	bool pitch_on;
	float pitch_ratio;

	/* telephone */
	bool tele_on;
	/* shared biquad coefficients (normalised, a0 = 1) */
	float hp_b0, hp_b1, hp_b2, hp_a1, hp_a2;
	float lp_b0, lp_b1, lp_b2, lp_a1, lp_a2;

	/* distortion */
	bool drive_on;
	float drive;

	/* ring modulator */
	bool ring_on;
	float ring_freq, ring_mix;

	/* bitcrush */
	bool crush_on;
	float crush_q; /* quantisation levels */
	int crush_down;

	/* tremolo */
	bool trem_on;
	float trem_rate, trem_depth;

	/* echo */
	bool echo_on;
	uint32_t echo_delay;
	float echo_fb, echo_mix;
};

/* ---- helpers ---------------------------------------------------------- */

static inline float db_to_lin(float db)
{
	return powf(10.0f, db / 20.0f);
}

static void biquad_lowpass(float fs, float f0, float q, float *b0, float *b1, float *b2, float *a1, float *a2)
{
	float w0 = TWO_PI_F * f0 / fs;
	float c = cosf(w0), s = sinf(w0);
	float alpha = s / (2.0f * q);
	float a0 = 1.0f + alpha;
	*b0 = ((1.0f - c) / 2.0f) / a0;
	*b1 = (1.0f - c) / a0;
	*b2 = ((1.0f - c) / 2.0f) / a0;
	*a1 = (-2.0f * c) / a0;
	*a2 = (1.0f - alpha) / a0;
}

static void biquad_highpass(float fs, float f0, float q, float *b0, float *b1, float *b2, float *a1, float *a2)
{
	float w0 = TWO_PI_F * f0 / fs;
	float c = cosf(w0), s = sinf(w0);
	float alpha = s / (2.0f * q);
	float a0 = 1.0f + alpha;
	*b0 = ((1.0f + c) / 2.0f) / a0;
	*b1 = (-(1.0f + c)) / a0;
	*b2 = ((1.0f + c) / 2.0f) / a0;
	*a1 = (-2.0f * c) / a0;
	*a2 = (1.0f - alpha) / a0;
}

static inline float biquad_run(struct biquad *bq, float x, float b0, float b1, float b2, float a1, float a2)
{
	float y = b0 * x + b1 * bq->x1 + b2 * bq->x2 - a1 * bq->y1 - a2 * bq->y2;
	bq->x2 = bq->x1;
	bq->x1 = x;
	bq->y2 = bq->y1;
	bq->y1 = y;
	return y;
}

static inline float pitch_run(struct fx_chan *c, float in, float ratio)
{
	c->pbuf[c->pw] = in;

	float out = 0.0f;
	for (int k = 0; k < 2; k++) {
		float r = c->pr + (k ? (float)GRAIN * 0.5f : 0.0f);
		while (r >= (float)GRAIN)
			r -= (float)GRAIN;
		int i0 = (int)r;
		float frac = r - (float)i0;
		int i1 = (i0 + 1) % GRAIN;
		float s = c->pbuf[i0] * (1.0f - frac) + c->pbuf[i1] * frac;
		float dist = (float)(((int)c->pw - i0 + GRAIN) % GRAIN) / (float)GRAIN;
		float win = 1.0f - fabsf(2.0f * dist - 1.0f); /* triangular crossfade */
		out += s * win;
	}

	c->pr += ratio;
	while (c->pr >= (float)GRAIN)
		c->pr -= (float)GRAIN;
	c->pw = (c->pw + 1) % GRAIN;
	return out;
}

/* ---- audio processing ------------------------------------------------- */

static void process_channel(struct voice_fx *v, struct fx_chan *c, float *samples, uint32_t n, uint64_t t0)
{
	const float fs = (float)v->sample_rate;

	for (uint32_t i = 0; i < n; i++) {
		uint64_t t = t0 + i;
		float x = samples[i] * v->in_gain;

		if (v->pitch_on)
			x = pitch_run(c, x, v->pitch_ratio);

		if (v->tele_on) {
			x = biquad_run(&c->hp, x, v->hp_b0, v->hp_b1, v->hp_b2, v->hp_a1, v->hp_a2);
			x = biquad_run(&c->lp, x, v->lp_b0, v->lp_b1, v->lp_b2, v->lp_a1, v->lp_a2);
		}

		if (v->drive_on)
			x = tanhf(x * v->drive);

		if (v->ring_on) {
			float carrier = sinf(TWO_PI_F * v->ring_freq * (float)t / fs);
			x = x * (1.0f - v->ring_mix) + (x * carrier) * v->ring_mix;
		}

		if (v->crush_on) {
			if (--c->crush_cnt <= 0) {
				c->crush_cnt = v->crush_down;
				c->crush_hold = roundf(x * v->crush_q) / v->crush_q;
			}
			x = c->crush_hold;
		}

		if (v->trem_on) {
			float lfo = 0.5f * (1.0f + sinf(TWO_PI_F * v->trem_rate * (float)t / fs));
			x *= (1.0f - v->trem_depth * lfo);
		}

		if (v->echo_on && c->echo && v->echo_len > 0) {
			uint32_t L = v->echo_len;
			uint32_t d = v->echo_delay < L ? v->echo_delay : (L - 1);
			uint32_t r = (c->echo_w + L - d) % L;
			float delayed = c->echo[r];
			c->echo[c->echo_w] = x + delayed * v->echo_fb;
			x = x * (1.0f - v->echo_mix) + delayed * v->echo_mix;
			c->echo_w = (c->echo_w + 1) % L;
		}

		samples[i] = x * v->out_gain;
	}
}

static struct obs_audio_data *voice_fx_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct voice_fx *v = data;
	uint64_t t0 = v->t;

	for (size_t ch = 0; ch < (size_t)MAX_AV_PLANES; ch++) {
		float *samples = (float *)audio->data[ch];
		if (!samples)
			continue;
		process_channel(v, &v->ch[ch], samples, audio->frames, t0);
	}

	v->t += audio->frames;
	return audio;
}

/* ---- source callbacks ------------------------------------------------- */

static const char *voice_fx_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("VoiceFX");
}

static void voice_fx_update(void *data, obs_data_t *s)
{
	struct voice_fx *v = data;
	float fs = (float)v->sample_rate;

	v->in_gain = db_to_lin((float)obs_data_get_double(s, "in_gain"));
	v->out_gain = db_to_lin((float)obs_data_get_double(s, "out_gain"));

	v->pitch_on = obs_data_get_bool(s, "pitch_enable");
	v->pitch_ratio = powf(2.0f, (float)obs_data_get_double(s, "pitch_semitones") / 12.0f);

	v->tele_on = obs_data_get_bool(s, "tele_enable");
	biquad_highpass(fs, 300.0f, 0.707f, &v->hp_b0, &v->hp_b1, &v->hp_b2, &v->hp_a1, &v->hp_a2);
	biquad_lowpass(fs, 3400.0f, 0.707f, &v->lp_b0, &v->lp_b1, &v->lp_b2, &v->lp_a1, &v->lp_a2);

	v->drive_on = obs_data_get_bool(s, "drive_enable");
	v->drive = (float)obs_data_get_double(s, "drive_amount");

	v->ring_on = obs_data_get_bool(s, "ring_enable");
	v->ring_freq = (float)obs_data_get_double(s, "ring_freq");
	v->ring_mix = (float)obs_data_get_double(s, "ring_mix");

	v->crush_on = obs_data_get_bool(s, "crush_enable");
	float bits = (float)obs_data_get_double(s, "crush_bits");
	v->crush_q = powf(2.0f, bits) - 1.0f;
	if (v->crush_q < 1.0f)
		v->crush_q = 1.0f;
	v->crush_down = (int)obs_data_get_double(s, "crush_down");
	if (v->crush_down < 1)
		v->crush_down = 1;

	v->trem_on = obs_data_get_bool(s, "trem_enable");
	v->trem_rate = (float)obs_data_get_double(s, "trem_rate");
	v->trem_depth = (float)obs_data_get_double(s, "trem_depth");

	v->echo_on = obs_data_get_bool(s, "echo_enable");
	v->echo_delay = (uint32_t)(fs * (float)obs_data_get_double(s, "echo_time") / 1000.0f);
	v->echo_fb = (float)obs_data_get_double(s, "echo_fb");
	v->echo_mix = (float)obs_data_get_double(s, "echo_mix");
}

static void *voice_fx_create(obs_data_t *settings, obs_source_t *context)
{
	struct voice_fx *v = bzalloc(sizeof(struct voice_fx));
	v->context = context;

	struct obs_audio_info oai;
	v->sample_rate = obs_get_audio_info(&oai) ? oai.samples_per_sec : 48000;

	/* allocate echo lines once (1 second max) to avoid realloc races */
	v->echo_len = v->sample_rate + 1;
	for (size_t ch = 0; ch < (size_t)MAX_AV_PLANES; ch++)
		v->ch[ch].echo = bzalloc(sizeof(float) * v->echo_len);

	voice_fx_update(v, settings);
	return v;
}

static void voice_fx_destroy(void *data)
{
	struct voice_fx *v = data;
	for (size_t ch = 0; ch < (size_t)MAX_AV_PLANES; ch++)
		bfree(v->ch[ch].echo);
	bfree(v);
}

static void voice_fx_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "in_gain", 0.0);
	obs_data_set_default_double(s, "out_gain", 0.0);

	obs_data_set_default_bool(s, "pitch_enable", false);
	obs_data_set_default_double(s, "pitch_semitones", -5.0);

	obs_data_set_default_bool(s, "tele_enable", false);

	obs_data_set_default_bool(s, "drive_enable", false);
	obs_data_set_default_double(s, "drive_amount", 5.0);

	obs_data_set_default_bool(s, "ring_enable", false);
	obs_data_set_default_double(s, "ring_freq", 120.0);
	obs_data_set_default_double(s, "ring_mix", 1.0);

	obs_data_set_default_bool(s, "crush_enable", false);
	obs_data_set_default_double(s, "crush_bits", 6.0);
	obs_data_set_default_double(s, "crush_down", 4.0);

	obs_data_set_default_bool(s, "trem_enable", false);
	obs_data_set_default_double(s, "trem_rate", 5.0);
	obs_data_set_default_double(s, "trem_depth", 0.7);

	obs_data_set_default_bool(s, "echo_enable", false);
	obs_data_set_default_double(s, "echo_time", 180.0);
	obs_data_set_default_double(s, "echo_fb", 0.35);
	obs_data_set_default_double(s, "echo_mix", 0.35);
}

static obs_properties_t *voice_fx_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();

	obs_properties_add_float_slider(p, "in_gain", obs_module_text("VoiceFX.InGain"), -24.0, 24.0, 0.5);

	obs_properties_t *g;

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "pitch_semitones", obs_module_text("VoiceFX.Pitch.Semitones"), -12.0, 12.0,
					1.0);
	obs_properties_add_group(p, "pitch_enable", obs_module_text("VoiceFX.Pitch"), OBS_GROUP_CHECKABLE, g);

	obs_properties_add_bool(p, "tele_enable", obs_module_text("VoiceFX.Telephone"));

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "drive_amount", obs_module_text("VoiceFX.Drive.Amount"), 1.0, 50.0, 0.5);
	obs_properties_add_group(p, "drive_enable", obs_module_text("VoiceFX.Drive"), OBS_GROUP_CHECKABLE, g);

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "ring_freq", obs_module_text("VoiceFX.Ring.Freq"), 10.0, 2000.0, 1.0);
	obs_properties_add_float_slider(g, "ring_mix", obs_module_text("VoiceFX.Ring.Mix"), 0.0, 1.0, 0.01);
	obs_properties_add_group(p, "ring_enable", obs_module_text("VoiceFX.Ring"), OBS_GROUP_CHECKABLE, g);

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "crush_bits", obs_module_text("VoiceFX.Crush.Bits"), 1.0, 16.0, 1.0);
	obs_properties_add_float_slider(g, "crush_down", obs_module_text("VoiceFX.Crush.Down"), 1.0, 50.0, 1.0);
	obs_properties_add_group(p, "crush_enable", obs_module_text("VoiceFX.Crush"), OBS_GROUP_CHECKABLE, g);

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "trem_rate", obs_module_text("VoiceFX.Trem.Rate"), 0.1, 20.0, 0.1);
	obs_properties_add_float_slider(g, "trem_depth", obs_module_text("VoiceFX.Trem.Depth"), 0.0, 1.0, 0.01);
	obs_properties_add_group(p, "trem_enable", obs_module_text("VoiceFX.Trem"), OBS_GROUP_CHECKABLE, g);

	g = obs_properties_create();
	obs_properties_add_float_slider(g, "echo_time", obs_module_text("VoiceFX.Echo.Time"), 10.0, 1000.0, 1.0);
	obs_properties_add_float_slider(g, "echo_fb", obs_module_text("VoiceFX.Echo.Feedback"), 0.0, 0.95, 0.01);
	obs_properties_add_float_slider(g, "echo_mix", obs_module_text("VoiceFX.Echo.Mix"), 0.0, 1.0, 0.01);
	obs_properties_add_group(p, "echo_enable", obs_module_text("VoiceFX.Echo"), OBS_GROUP_CHECKABLE, g);

	obs_properties_add_float_slider(p, "out_gain", obs_module_text("VoiceFX.OutGain"), -24.0, 24.0, 0.5);

	return p;
}

static struct obs_source_info voice_fx_info = {
	.id = "meketreve_voice_fx",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = voice_fx_get_name,
	.create = voice_fx_create,
	.destroy = voice_fx_destroy,
	.update = voice_fx_update,
	.filter_audio = voice_fx_filter_audio,
	.get_defaults = voice_fx_defaults,
	.get_properties = voice_fx_properties,
};

void voice_fx_register(void)
{
	obs_register_source(&voice_fx_info);
}
