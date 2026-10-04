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

#include "subathon.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Subathon {

namespace {

double number(const QJsonObject &o, const char *key, double fallback, double low, double high)
{
	const QJsonValue v = o.value(QLatin1String(key));
	const double n = v.isDouble() ? v.toDouble() : fallback;
	return std::isfinite(n) ? std::clamp(n, low, high) : fallback;
}

bool flag(const QJsonObject &o, const char *key, bool fallback)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() ? v.toBool() : fallback;
}

QString color(const QJsonObject &o, const char *key, const QString &fallback)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString v = o.value(QLatin1String(key)).toString();
	return re.match(v).hasMatch() ? v : fallback;
}

qint64 clampSeconds(qint64 seconds)
{
	return std::clamp<qint64>(seconds, 0, kMaxSeconds);
}

} // namespace

QJsonObject normalizeConfig(const QJsonObject &stored)
{
	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	return QJsonObject{
		{QStringLiteral("title"), stored.value(QStringLiteral("title")).toString().trimmed().left(80)},
		{QStringLiteral("perSub"), number(stored, "perSub", 60, 0, 3600)},
		{QStringLiteral("perGiftSub"), number(stored, "perGiftSub", 60, 0, 3600)},
		{QStringLiteral("perBits100"), number(stored, "perBits100", 12, 0, 3600)},
		{QStringLiteral("perDonation"), number(stored, "perDonation", 12, 0, 3600)},
		{QStringLiteral("perMember"), number(stored, "perMember", 60, 0, 3600)},
		{QStringLiteral("perFollow"), number(stored, "perFollow", 0, 0, 3600)},
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("fontSize"), number(stored, "fontSize", 64, 12, 200)},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("accent"), color(stored, "accent", QStringLiteral("#8B5CF6"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), number(stored, "bubbleOpacity", 45, 0, 100)},
		{QStringLiteral("shadow"), flag(stored, "shadow", true)},
	};
}

qint64 secondsFor(const QJsonObject &config, const Alerts::Event &event)
{
	if (event.test)
		return 0;
	const auto per = [&config](const char *key) {
		return config.value(QLatin1String(key)).toDouble();
	};
	const QString &type = event.type;
	double seconds = 0;
	if (type == QLatin1String("sub") || type == QLatin1String("resub"))
		seconds = per("perSub");
	else if (type == QLatin1String("giftsub"))
		seconds = per("perGiftSub") * std::max(1.0, event.value);
	else if (type == QLatin1String("bits"))
		seconds = per("perBits100") * std::max(0.0, event.value) / 100.0;
	else if (type == QLatin1String("donation"))
		seconds = per("perDonation") * std::max(0.0, event.value);
	else if (type == QLatin1String("membership"))
		seconds = per("perMember");
	else if (type == QLatin1String("follow"))
		seconds = per("perFollow");
	return std::isfinite(seconds) ? clampSeconds(static_cast<qint64>(std::llround(seconds))) : 0;
}

Timer::State Timer::state(qint64 nowMs) const
{
	if (!m_started)
		return State::Idle;
	if (m_paused)
		return m_left > 0 ? State::Paused : State::Ended;
	return nowMs < m_endsAt ? State::Running : State::Ended;
}

qint64 Timer::remainingMs(qint64 nowMs) const
{
	if (!m_started)
		return 0;
	return m_paused ? m_left : std::max<qint64>(0, m_endsAt - nowMs);
}

void Timer::start(qint64 seconds, qint64 nowMs)
{
	m_started = true;
	m_paused = false;
	m_endsAt = nowMs + clampSeconds(seconds) * 1000;
	m_left = 0;
}

bool Timer::pause(qint64 nowMs)
{
	if (state(nowMs) != State::Running)
		return false;
	m_left = m_endsAt - nowMs;
	m_paused = true;
	return true;
}

bool Timer::resume(qint64 nowMs)
{
	if (state(nowMs) != State::Paused)
		return false;
	m_endsAt = nowMs + m_left;
	m_paused = false;
	return true;
}

bool Timer::add(qint64 seconds, qint64 nowMs)
{
	const State s = state(nowMs);
	if (s != State::Running && s != State::Paused)
		return false;
	setLeft(remainingMs(nowMs) + seconds * 1000, nowMs);
	return true;
}

bool Timer::set(qint64 seconds, qint64 nowMs)
{
	if (state(nowMs) == State::Idle)
		return false;
	/* Setting time on an ended timer starts it again. */
	setLeft(clampSeconds(seconds) * 1000, nowMs);
	return true;
}

void Timer::setLeft(qint64 ms, qint64 nowMs)
{
	ms = std::clamp<qint64>(ms, 0, kMaxSeconds * 1000);
	if (m_paused && ms > 0)
		m_left = ms;
	else {
		m_paused = false;
		m_endsAt = nowMs + ms;
	}
}

void Timer::reset()
{
	*this = Timer();
}

QJsonObject Timer::toJson(qint64 nowMs) const
{
	static const char *const names[] = {"idle", "running", "paused", "ended"};
	return QJsonObject{{QStringLiteral("state"), QLatin1String(names[static_cast<int>(state(nowMs))])},
			   {QStringLiteral("remainingMs"), static_cast<double>(remainingMs(nowMs))}};
}

QJsonObject Timer::save() const
{
	return QJsonObject{{QStringLiteral("started"), m_started},
			   {QStringLiteral("paused"), m_paused},
			   {QStringLiteral("endsAt"), static_cast<double>(m_endsAt)},
			   {QStringLiteral("left"), static_cast<double>(m_left)}};
}

void Timer::load(const QJsonObject &stored)
{
	m_started = stored.value(QStringLiteral("started")).toBool();
	m_paused = stored.value(QStringLiteral("paused")).toBool();
	m_endsAt = static_cast<qint64>(stored.value(QStringLiteral("endsAt")).toDouble());
	m_left = std::clamp<qint64>(static_cast<qint64>(stored.value(QStringLiteral("left")).toDouble()), 0,
				    kMaxSeconds * 1000);
}

} // namespace Subathon
