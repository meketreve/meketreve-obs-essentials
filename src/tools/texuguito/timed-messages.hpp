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
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/* A message the bot posts by itself every few minutes ("follow us on…"). */
struct TimedMessage {
	QString id;
	QString text;
	int minutes = 15;
	/* Chat lines since its last post: a quiet chat is not flooded. */
	int minLines = 5;
	bool enabled = true;
	/* "twitch", "youtube", "kick"; empty = every platform. */
	QStringList platforms;
};

/* The automatic messages and their clocks, saved as timed-messages.json.
 * No OBS or timer here: the dock asks due() now and then. */
class TimedMessages {
public:
	static constexpr int kMinMinutes = 1;
	static constexpr int kMaxMinutes = 720;
	static constexpr int kMaxLines = 500;
	static constexpr int kMaxText = 450;
	static constexpr int kMaxMessages = 50;
	/* Two automatic messages never go out closer than this. */
	static constexpr qint64 kGapMs = 60000;

	explicit TimedMessages(const QString &path);

	const QList<TimedMessage> &messages() const { return m_messages; }
	const TimedMessage *find(const QString &id) const;
	bool onlyLive() const { return m_onlyLive; }
	void setOnlyLive(bool on);

	/* Creates (empty id) or changes a message; its clock starts at now when
	 * new. The id, or empty with the reason in *error ("text", "count"). */
	QString set(TimedMessage message, qint64 now, QString *error = nullptr);
	bool remove(const QString &id);

	/* One chat line from anyone but the bot. */
	void chatLine();
	/* Every clock starts again at now: the bot was turned on or the stream
	 * went live, so nothing goes out the moment it starts. */
	void restart(qint64 now);
	/* The message to post at now (the most overdue one), and its clock
	 * starts again; nothing while the last one is under kGapMs old. */
	std::optional<TimedMessage> due(qint64 now);
	/* Posted by hand ("send now"): its clock starts again too. */
	void markSent(const QString &id, qint64 now);

	QJsonArray toJson() const;
	static QJsonObject toJson(const TimedMessage &message);
	static TimedMessage fromJson(const QJsonObject &object);
	static bool validPlatform(const QString &name);

private:
	struct Clock {
		qint64 since = 0;
		int lines = 0;
	};
	void load();
	void save() const;

	QString m_path;
	QList<TimedMessage> m_messages;
	QHash<QString, Clock> m_clocks;
	bool m_onlyLive = true;
	qint64 m_lastPost = -1;
};
