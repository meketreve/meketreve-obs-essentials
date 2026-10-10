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
#include "pngtuber-logic.hpp"

#include <algorithm>
#include <cmath>

namespace PngTuber {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBobHz = 3.0;
} // namespace

Avatar::Avatar(std::function<double()> rand01) : m_rand(std::move(rand01))
{
	scheduleBlink();
}

void Avatar::setSettings(const Settings &settings)
{
	const bool newBlinkRange = settings.blinkMinS != m_settings.blinkMinS ||
				   settings.blinkMaxS != m_settings.blinkMaxS;
	m_settings = settings;
	m_settings.blinkMinS = std::max(0.2, m_settings.blinkMinS);
	m_settings.blinkMaxS = std::max(m_settings.blinkMinS, m_settings.blinkMaxS);
	m_settings.blinkMs = std::max(0.0, m_settings.blinkMs);
	m_settings.holdMs = std::max(0.0, m_settings.holdMs);
	m_settings.hopPx = std::max(0.0, m_settings.hopPx);
	m_settings.hopMs = std::max(1.0, m_settings.hopMs);
	m_settings.bobPx = std::max(0.0, m_settings.bobPx);
	/* A new blink range counts from now. */
	if (newBlinkRange && m_blinkLeft <= 0.0)
		scheduleBlink();
}

void Avatar::scheduleBlink()
{
	const double r = m_rand ? std::clamp(m_rand(), 0.0, 1.0) : 0.5;
	m_nextBlink = m_settings.blinkMinS + (m_settings.blinkMaxS - m_settings.blinkMinS) * r;
}

void Avatar::tick(double seconds)
{
	if (seconds <= 0.0)
		return;

	/* Talking: on as soon as the voice is loud enough, off once it has
	 * been quiet for the hold time (so it does not flicker between words). */
	if (m_levelDb >= m_settings.thresholdDb) {
		if (!m_talking) {
			m_talking = true;
			m_talkTime = 0.0;
			if (m_settings.hopPx > 0.0)
				m_hopAt = 0.0;
		}
		m_quietFor = 0.0;
	} else if (m_talking) {
		m_quietFor += seconds;
		if (m_quietFor * 1000.0 >= m_settings.holdMs)
			m_talking = false;
	}
	if (m_talking)
		m_talkTime += seconds;

	if (m_hopAt >= 0.0) {
		m_hopAt += seconds;
		if (m_hopAt * 1000.0 >= m_settings.hopMs)
			m_hopAt = -1.0;
	}

	/* Blinking: eyes closed for blinkMs, then open until the next one. */
	if (m_blinkLeft > 0.0) {
		m_blinkLeft -= seconds;
		if (m_blinkLeft <= 0.0) {
			m_blinkLeft = 0.0;
			scheduleBlink();
		}
	} else if (m_settings.blink) {
		m_nextBlink -= seconds;
		if (m_nextBlink <= 0.0)
			m_blinkLeft = m_settings.blinkMs / 1000.0;
	}
}

double Avatar::lift() const
{
	double up = 0.0;
	/* The hop: half a sine, up and back down. */
	if (m_hopAt >= 0.0)
		up += m_settings.hopPx * std::sin(kPi * std::clamp(m_hopAt * 1000.0 / m_settings.hopMs, 0.0, 1.0));
	if (m_talking && m_settings.bobPx > 0.0)
		up += m_settings.bobPx * 0.5 * (1.0 - std::cos(2.0 * kPi * kBobHz * m_talkTime));
	return up;
}

double Avatar::maxLift() const
{
	return m_settings.hopPx + m_settings.bobPx;
}

double Avatar::levelDb(const float *samples, unsigned count)
{
	if (!samples || count == 0)
		return -120.0;
	double sum = 0.0;
	for (unsigned i = 0; i < count; ++i)
		sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
	const double rms = std::sqrt(sum / count);
	return rms > 1e-6 ? 20.0 * std::log10(rms) : -120.0;
}

} // namespace PngTuber
