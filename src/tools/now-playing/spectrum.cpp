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

#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLow = 40.0, kHigh = 11000.0;

} // namespace

Spectrum::Spectrum(int sampleRate) : m_rate(sampleRate > 0 ? sampleRate : 48000)
{
	/* ~12 Hz per bin at 24 kHz and 48 kHz alike. */
	m_n = m_rate > 32000 ? 4096 : 2048;
	m_ring.assign(static_cast<size_t>(m_n), 0.0f);
	m_re.assign(static_cast<size_t>(m_n), 0.0f);
	m_im.assign(static_cast<size_t>(m_n), 0.0f);
	m_mags.assign(static_cast<size_t>(m_n / 2 + 1), 0.0f);
	m_window.resize(static_cast<size_t>(m_n));
	m_rev.resize(static_cast<size_t>(m_n));
	m_cos.resize(static_cast<size_t>(m_n / 2));
	m_sin.resize(static_cast<size_t>(m_n / 2));

	int bits = 0;
	while ((1 << bits) < m_n)
		bits++;
	for (int i = 0; i < m_n; i++) {
		m_window[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * i / (m_n - 1)));
		unsigned r = 0;
		for (int b = 0; b < bits; b++)
			r = (r << 1) | ((static_cast<unsigned>(i) >> b) & 1u);
		m_rev[static_cast<size_t>(i)] = r;
	}
	for (int i = 0; i < m_n / 2; i++) {
		m_cos[static_cast<size_t>(i)] = static_cast<float>(std::cos(2 * kPi * i / m_n));
		m_sin[static_cast<size_t>(i)] = static_cast<float>(-std::sin(2 * kPi * i / m_n));
	}

	const double binHz = static_cast<double>(m_rate) / m_n;
	m_bands.resize(kBands);
	for (int i = 0; i < kBands; i++) {
		const double f0 = kLow * std::pow(kHigh / kLow, static_cast<double>(i) / kBands);
		const double f1 = kLow * std::pow(kHigh / kLow, static_cast<double>(i + 1) / kBands);
		const double fc = std::sqrt(f0 * f1);
		Band &band = m_bands[static_cast<size_t>(i)];
		band.k0 = static_cast<int>(std::floor(f0 / binHz));
		band.k1 = std::max(static_cast<int>(std::ceil(f1 / binHz)), band.k0 + 1);
		band.narrow = f1 - f0 < 1.5 * binHz;
		band.kc = fc / binHz;
		band.tilt = 2 * std::log2(fc / 1000);
		band.fc = fc;
	}
}

void Spectrum::push(const float *samples, size_t count)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t n = m_ring.size();
	for (size_t i = 0; i < count; i++) {
		m_ring[m_write] = samples[i];
		m_write = (m_write + 1) % n;
	}
}

void Spectrum::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::fill(m_ring.begin(), m_ring.end(), 0.0f);
	m_write = 0;
}

double Spectrum::bandCenter(int band) const
{
	return band >= 0 && band < kBands ? m_bands[static_cast<size_t>(band)].fc : 0;
}

Spectrum::Frame Spectrum::analyse()
{
	const size_t n = static_cast<size_t>(m_n);
	double sum2 = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (size_t i = 0; i < n; i++) {
			const float s = m_ring[(m_write + i) % n];
			sum2 += static_cast<double>(s) * s;
			m_re[m_rev[i]] = s * m_window[i];
			m_im[m_rev[i]] = 0.0f;
		}
	}
	/* Iterative radix-2 FFT. */
	for (size_t size = 2; size <= n; size <<= 1) {
		const size_t half = size >> 1, step = n / size;
		for (size_t start = 0; start < n; start += size) {
			for (size_t k = 0; k < half; k++) {
				const float c = m_cos[k * step], s = m_sin[k * step];
				const size_t a = start + k, b = a + half;
				const float tr = m_re[b] * c - m_im[b] * s, ti = m_re[b] * s + m_im[b] * c;
				m_re[b] = m_re[a] - tr;
				m_im[b] = m_im[a] - ti;
				m_re[a] += tr;
				m_im[a] += ti;
			}
		}
	}
	/* Amplitude of a sine (the Hann window halves it). */
	for (size_t k = 0; k <= n / 2; k++)
		m_mags[k] = std::hypot(m_re[k], m_im[k]) * 4.0f / static_cast<float>(n);

	Frame frame;
	frame.rmsDb = static_cast<float>(20 * std::log10(std::sqrt(sum2 / static_cast<double>(n)) + 1e-9));
	frame.bands.resize(kBands);
	const int last = m_n / 2;
	for (int i = 0; i < kBands; i++) {
		const Band &band = m_bands[static_cast<size_t>(i)];
		double m = 0;
		if (band.narrow) {
			const int k = std::min(static_cast<int>(band.kc), last - 1);
			const double f = band.kc - k;
			m = m_mags[static_cast<size_t>(k)] * (1 - f) + m_mags[static_cast<size_t>(k + 1)] * f;
		} else {
			for (int k = band.k0; k < std::min(band.k1, last + 1); k++)
				m = std::max(m, static_cast<double>(m_mags[static_cast<size_t>(k)]));
		}
		frame.bands[i] = static_cast<float>(20 * std::log10(m + 1e-9) + band.tilt);
	}
	return frame;
}
