/*
Meketreve OBS Essentials - Texuguito bot
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
#include "boss.hpp"
#include "bot-data.hpp"

#include <QDateTime>

#include <algorithm>

namespace {

int clampInt(const QJsonValue &v, int fallback, int low, int high)
{
	return std::clamp(v.isDouble() ? static_cast<int>(std::clamp(v.toDouble(), -1e9, 1e9)) : fallback, low, high);
}

} // namespace

int BossFight::damage(Hit hit)
{
	switch (hit) {
	case Hit::Message:
		return 2;
	case Hit::Attack:
		return 10;
	case Hit::Follow:
		return 30;
	case Hit::Support:
	case Hit::Sub:
		break;
	}
	return 100;
}

qint64 BossFight::waitMs(Hit hit)
{
	switch (hit) {
	case Hit::Message:
		return 5000;
	case Hit::Attack:
		return 10000;
	case Hit::Support:
	case Hit::Sub:
	case Hit::Follow:
		break;
	}
	return 0;
}

BossFight::BossFight(const QString &path, Clock clock) : m_path(path), m_clock(std::move(clock))
{
	if (!m_clock)
		m_clock = []() {
			return QDateTime::currentMSecsSinceEpoch();
		};
	load();
}

void BossFight::load()
{
	const QJsonObject raw = BotData::readJsonFile(m_path);
	m_enabled = raw.value(QStringLiteral("enabled")).toBool(true);
	m_name = raw.value(QStringLiteral("name")).toString().simplified().left(kMaxName);
	m_baseHp = clampInt(raw.value(QStringLiteral("baseHp")), 100, 1, kMaxHp);
	m_hpPerViewer = clampInt(raw.value(QStringLiteral("hpPerViewer")), 30, 0, kMaxHp);
	m_minutes = clampInt(raw.value(QStringLiteral("minutes")), 5, 1, kMaxMinutes);
	m_autoMinutes = clampInt(raw.value(QStringLiteral("autoMinutes")), 0, 0, kMaxAutoMinutes);
	m_points = clampInt(raw.value(QStringLiteral("points")), 100, 0, kMaxPoints);
	m_xp = clampInt(raw.value(QStringLiteral("xp")), 30, 0, kMaxXp);
	m_mvpPoints = clampInt(raw.value(QStringLiteral("mvpPoints")), 50, 0, kMaxPoints);
}

void BossFight::save() const
{
	BotData::writeJsonFile(m_path, settingsJson());
}

QJsonObject BossFight::settingsJson() const
{
	return QJsonObject{{QStringLiteral("enabled"), m_enabled},    {QStringLiteral("name"), m_name},
			   {QStringLiteral("baseHp"), m_baseHp},      {QStringLiteral("hpPerViewer"), m_hpPerViewer},
			   {QStringLiteral("minutes"), m_minutes},    {QStringLiteral("autoMinutes"), m_autoMinutes},
			   {QStringLiteral("points"), m_points},      {QStringLiteral("xp"), m_xp},
			   {QStringLiteral("mvpPoints"), m_mvpPoints}};
}

void BossFight::applySettings(const QJsonObject &settings)
{
	m_enabled = settings.value(QStringLiteral("enabled")).toBool(m_enabled);
	if (settings.contains(QStringLiteral("name")))
		m_name = settings.value(QStringLiteral("name")).toString().simplified().left(kMaxName);
	m_baseHp = clampInt(settings.value(QStringLiteral("baseHp")), m_baseHp, 1, kMaxHp);
	m_hpPerViewer = clampInt(settings.value(QStringLiteral("hpPerViewer")), m_hpPerViewer, 0, kMaxHp);
	m_minutes = clampInt(settings.value(QStringLiteral("minutes")), m_minutes, 1, kMaxMinutes);
	const int autoMinutes =
		clampInt(settings.value(QStringLiteral("autoMinutes")), m_autoMinutes, 0, kMaxAutoMinutes);
	/* A new interval counts from now. */
	if (autoMinutes != m_autoMinutes && m_autoFrom > 0)
		m_autoFrom = m_clock();
	m_autoMinutes = autoMinutes;
	m_points = clampInt(settings.value(QStringLiteral("points")), m_points, 0, kMaxPoints);
	m_xp = clampInt(settings.value(QStringLiteral("xp")), m_xp, 0, kMaxXp);
	m_mvpPoints = clampInt(settings.value(QStringLiteral("mvpPoints")), m_mvpPoints, 0, kMaxPoints);
	save();
}

void BossFight::restoreDefaults()
{
	const bool enabled = m_enabled;
	applySettings(QJsonObject{{QStringLiteral("name"), QString()},
				  {QStringLiteral("baseHp"), 100},
				  {QStringLiteral("hpPerViewer"), 30},
				  {QStringLiteral("minutes"), 5},
				  {QStringLiteral("autoMinutes"), 0},
				  {QStringLiteral("points"), 100},
				  {QStringLiteral("xp"), 30},
				  {QStringLiteral("mvpPoints"), 50},
				  {QStringLiteral("enabled"), enabled}});
}

bool BossFight::start(int viewers)
{
	if (!m_enabled || m_active)
		return false;
	const qint64 hp = m_baseHp + static_cast<qint64>(m_hpPerViewer) * std::max(0, viewers);
	m_maxHp = static_cast<int>(std::clamp<qint64>(hp, 1, kMaxHp));
	m_hp = m_maxHp;
	m_until = m_clock() + m_minutes * 60000LL;
	m_damage.clear();
	m_lastHit.clear();
	m_active = true;
	return true;
}

int BossFight::hit(const QString &key, Hit hit)
{
	const qint64 now = m_clock();
	if (!m_active || m_hp <= 0 || now >= m_until || key.isEmpty())
		return 0;
	const QString slot = QString::number(static_cast<int>(hit)) + QLatin1Char(':') + key.toLower();
	const auto last = m_lastHit.constFind(slot);
	if (last != m_lastHit.constEnd() && now - *last < waitMs(hit))
		return 0;
	m_lastHit.insert(slot, now);
	const int done = std::min(damage(hit), m_hp);
	m_hp -= done;
	m_damage[key.toLower()] += done;
	return done;
}

qint64 BossFight::remainingMs() const
{
	return m_active ? std::max<qint64>(0, m_until - m_clock()) : 0;
}

std::optional<BossFight::Result> BossFight::finish()
{
	const qint64 now = m_clock();
	if (!m_active || (m_hp > 0 && now < m_until))
		return std::nullopt;
	Result r;
	r.defeated = m_hp <= 0;
	r.hpLeft = m_hp;
	for (auto it = m_damage.constBegin(); it != m_damage.constEnd(); ++it)
		r.damage.append({it.key(), it.value()});
	std::sort(r.damage.begin(), r.damage.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
		return a.second != b.second ? a.second > b.second : a.first < b.first;
	});
	m_active = false;
	m_damage.clear();
	m_lastHit.clear();
	if (m_autoFrom > 0)
		m_autoFrom = now;
	return r;
}

bool BossFight::autoDue(bool live)
{
	if (!live || !m_enabled || m_autoMinutes <= 0) {
		m_autoFrom = 0;
		return false;
	}
	const qint64 now = m_clock();
	if (m_autoFrom <= 0) {
		m_autoFrom = now;
		return false;
	}
	return !m_active && now - m_autoFrom >= m_autoMinutes * 60000LL;
}
