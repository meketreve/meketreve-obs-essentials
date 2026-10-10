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
#pragma once

#include <functional>

/* What a PNGTuber avatar does, apart from drawing: talks while the voice is
 * above a threshold (and a moment after), blinks now and then, and hops
 * when it starts talking. No OBS here, so it runs in tests. */
namespace PngTuber {

struct Settings {
	double thresholdDb = -38.0; /* voice above this = talking */
	double holdMs = 180.0;      /* keeps talking this long after the voice drops */
	bool blink = true;
	double blinkMinS = 2.5; /* time between blinks, random in [min, max] */
	double blinkMaxS = 6.0;
	double blinkMs = 140.0;
	double hopPx = 10.0; /* how high it hops when it starts talking */
	double hopMs = 220.0;
	double bobPx = 0.0; /* bobbing up and down while talking */
};

class Avatar {
public:
	/* rand01: a random number in [0, 1), for the blinks. */
	explicit Avatar(std::function<double()> rand01);

	void setSettings(const Settings &settings);
	const Settings &settings() const { return m_settings; }

	/* The voice level now, in dBFS (silence is very negative). */
	void setLevel(double db) { m_levelDb = db; }
	/* Moves time forward. */
	void tick(double seconds);

	bool talking() const { return m_talking; }
	bool eyesClosed() const { return m_blinkLeft > 0.0; }
	/* How far up to draw the avatar now, in pixels (>= 0). */
	double lift() const;
	/* The most lift() can be, for the size of the source. */
	double maxLift() const;

	/* RMS of samples in dBFS; -inf-ish (-120) for silence. */
	static double levelDb(const float *samples, unsigned count);

private:
	void scheduleBlink();

	std::function<double()> m_rand;
	Settings m_settings;
	double m_levelDb = -120.0;
	bool m_talking = false;
	double m_quietFor = 0.0; /* seconds under the threshold while talking */
	double m_nextBlink = 0.0;
	double m_blinkLeft = 0.0;
	double m_hopAt = -1.0; /* seconds since the hop started, < 0 = none */
	double m_talkTime = 0.0;
};

} // namespace PngTuber
