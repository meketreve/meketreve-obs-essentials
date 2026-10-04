/*
Meketreve OBS Essentials - Alerts
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

#include "alert-logic.hpp"

#include <QJsonObject>

/* The subathon timer at /subathon: a countdown that the events of the three
 * platforms push forward (a sub adds a minute, 100 bits add 12 seconds...).
 * The end is kept as a time of day, so it keeps running while OBS is closed.
 * Pure logic, so the tests run without OBS. */
namespace Subathon {

constexpr qint64 kMaxSeconds = 365LL * 24 * 3600;

/* {title, perSub, perGiftSub, perBits100, perDonation, perMember, perFollow,
 * font, fontSize, textColor, accent, bubbleColor, bubbleOpacity, shadow}:
 * seconds added per sub, per gifted sub, per 100 bits, per unit of money,
 * per YouTube member and per follow (0 = that event adds nothing). */
QJsonObject normalizeConfig(const QJsonObject &stored);

/* Seconds the event adds with that config (0 when nothing). */
qint64 secondsFor(const QJsonObject &config, const Alerts::Event &event);

class Timer {
public:
	enum class State { Idle, Running, Paused, Ended };

	State state(qint64 nowMs) const;
	qint64 remainingMs(qint64 nowMs) const;

	/* Starts over with that many seconds, running. */
	void start(qint64 seconds, qint64 nowMs);
	bool pause(qint64 nowMs);
	bool resume(qint64 nowMs);
	/* Adds (or with a negative value takes away) time while it runs or is
	 * paused; never below zero. False when idle or ended. */
	bool add(qint64 seconds, qint64 nowMs);
	/* Sets the time left, keeping it running or paused. */
	bool set(qint64 seconds, qint64 nowMs);
	void reset();

	/* {state: idle|running|paused|ended, remainingMs}. */
	QJsonObject toJson(qint64 nowMs) const;
	QJsonObject save() const;
	void load(const QJsonObject &stored);

private:
	void setLeft(qint64 ms, qint64 nowMs);

	bool m_started = false;
	bool m_paused = false;
	qint64 m_endsAt = 0; /* when running */
	qint64 m_left = 0;   /* when paused */
};

} // namespace Subathon
