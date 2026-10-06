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
#include "timed-messages.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

TimedMessages::TimedMessages(const QString &path) : m_path(path)
{
	load();
}

bool TimedMessages::validPlatform(const QString &name)
{
	return name == QLatin1String("twitch") || name == QLatin1String("youtube") || name == QLatin1String("kick");
}

QJsonObject TimedMessages::toJson(const TimedMessage &m)
{
	return QJsonObject{{QStringLiteral("id"), m.id},
			   {QStringLiteral("text"), m.text},
			   {QStringLiteral("minutes"), m.minutes},
			   {QStringLiteral("minLines"), m.minLines},
			   {QStringLiteral("enabled"), m.enabled},
			   {QStringLiteral("platforms"), QJsonArray::fromStringList(m.platforms)}};
}

TimedMessage TimedMessages::fromJson(const QJsonObject &o)
{
	TimedMessage m;
	m.id = o.value(QStringLiteral("id")).toString();
	m.text = o.value(QStringLiteral("text")).toString();
	m.minutes = o.value(QStringLiteral("minutes")).toInt(m.minutes);
	m.minLines = o.value(QStringLiteral("minLines")).toInt(m.minLines);
	m.enabled = o.value(QStringLiteral("enabled")).toBool(m.enabled);
	for (const QJsonValue v : o.value(QStringLiteral("platforms")).toArray())
		m.platforms.append(v.toString());
	return m;
}

QJsonArray TimedMessages::toJson() const
{
	QJsonArray list;
	for (const TimedMessage &m : m_messages)
		list.append(toJson(m));
	return list;
}

const TimedMessage *TimedMessages::find(const QString &id) const
{
	for (const TimedMessage &m : m_messages) {
		if (m.id == id)
			return &m;
	}
	return nullptr;
}

void TimedMessages::setOnlyLive(bool on)
{
	m_onlyLive = on;
	save();
}

QString TimedMessages::set(TimedMessage message, qint64 now, QString *error)
{
	message.text = message.text.simplified().left(kMaxText);
	if (message.text.isEmpty()) {
		if (error)
			*error = QStringLiteral("text");
		return QString();
	}
	message.minutes = std::clamp(message.minutes, kMinMinutes, kMaxMinutes);
	message.minLines = std::clamp(message.minLines, 0, kMaxLines);
	QStringList platforms;
	for (const QString &p : message.platforms) {
		if (validPlatform(p) && !platforms.contains(p))
			platforms.append(p);
	}
	message.platforms = platforms;

	for (TimedMessage &m : m_messages) {
		if (!message.id.isEmpty() && m.id == message.id) {
			m = message;
			save();
			return m.id;
		}
	}
	if (m_messages.size() >= kMaxMessages) {
		if (error)
			*error = QStringLiteral("count");
		return QString();
	}
	for (int n = 1;; n++) {
		message.id = QStringLiteral("m%1").arg(n);
		if (!find(message.id))
			break;
	}
	m_messages.append(message);
	m_clocks.insert(message.id, Clock{now, 0});
	save();
	return message.id;
}

bool TimedMessages::remove(const QString &id)
{
	const auto it =
		std::find_if(m_messages.begin(), m_messages.end(), [&id](const TimedMessage &m) { return m.id == id; });
	if (it == m_messages.end())
		return false;
	m_messages.erase(it);
	m_clocks.remove(id);
	save();
	return true;
}

void TimedMessages::chatLine()
{
	for (Clock &c : m_clocks)
		c.lines++;
}

void TimedMessages::restart(qint64 now)
{
	m_clocks.clear();
	for (const TimedMessage &m : m_messages)
		m_clocks.insert(m.id, Clock{now, 0});
	m_lastPost = -1;
}

std::optional<TimedMessage> TimedMessages::due(qint64 now)
{
	if (m_lastPost >= 0 && now - m_lastPost < kGapMs)
		return std::nullopt;
	const TimedMessage *best = nullptr;
	qint64 bestLate = -1;
	for (const TimedMessage &m : m_messages) {
		/* A message with no clock yet starts counting now. */
		if (!m_clocks.contains(m.id))
			m_clocks.insert(m.id, Clock{now, 0});
		const Clock c = m_clocks.value(m.id);
		const qint64 late = now - c.since - qint64(m.minutes) * 60000;
		if (m.enabled && late >= 0 && c.lines >= m.minLines && late > bestLate) {
			best = &m;
			bestLate = late;
		}
	}
	if (!best)
		return std::nullopt;
	const TimedMessage found = *best;
	markSent(found.id, now);
	return found;
}

void TimedMessages::markSent(const QString &id, qint64 now)
{
	m_clocks.insert(id, Clock{now, 0});
	m_lastPost = now;
}

void TimedMessages::load()
{
	QFile in(m_path);
	if (!in.open(QIODevice::ReadOnly))
		return;
	const QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
	m_onlyLive = root.value(QStringLiteral("onlyLive")).toBool(true);
	for (const QJsonValue v : root.value(QStringLiteral("messages")).toArray()) {
		TimedMessage m = fromJson(v.toObject());
		if (m.id.isEmpty() || find(m.id) || m.text.trimmed().isEmpty() || m_messages.size() >= kMaxMessages)
			continue;
		m.minutes = std::clamp(m.minutes, kMinMinutes, kMaxMinutes);
		m.minLines = std::clamp(m.minLines, 0, kMaxLines);
		m_messages.append(m);
	}
}

void TimedMessages::save() const
{
	if (m_path.isEmpty())
		return;
	QDir().mkpath(QFileInfo(m_path).path());
	QSaveFile out(m_path);
	if (out.open(QIODevice::WriteOnly)) {
		out.write(QJsonDocument(QJsonObject{{QStringLiteral("onlyLive"), m_onlyLive},
						    {QStringLiteral("messages"), toJson()}})
				  .toJson(QJsonDocument::Indented));
		out.commit();
	}
}
