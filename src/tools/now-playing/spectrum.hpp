/*
Meketreve OBS Essentials - Now Playing
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

#include <QVector>

#include <mutex>
#include <vector>

/* Bars of the Now Playing overlay: the last FFT window of an audio source,
 * cut in log-spaced bands (same analysis as the Toca do Texugo panel). The
 * audio thread pushes samples, the UI thread asks for the bands. Smoothing,
 * auto gain and drawing stay in the page. */
class Spectrum {
public:
	static constexpr int kBands = 64;

	explicit Spectrum(int sampleRate = 48000);

	int sampleRate() const { return m_rate; }
	int size() const { return m_n; }

	/* Mono samples in -1..1. Safe from any thread. */
	void push(const float *samples, size_t count);
	void clear();

	/* dB per band (with +2 dB per octave so music does not fall in the
	 * highs) and the RMS of the window in dB. */
	struct Frame {
		QVector<float> bands;
		float rmsDb = -120.0f;
	};
	Frame analyse();

	/* Center frequency of a band, exposed for tests. */
	double bandCenter(int band) const;

private:
	struct Band {
		int k0 = 0, k1 = 1;
		bool narrow = false;
		double kc = 0, tilt = 0, fc = 0;
	};

	int m_rate;
	int m_n;
	std::mutex m_mutex;
	std::vector<float> m_ring;
	size_t m_write = 0;

	std::vector<float> m_window, m_re, m_im, m_cos, m_sin, m_mags;
	std::vector<unsigned> m_rev;
	std::vector<Band> m_bands;
};
