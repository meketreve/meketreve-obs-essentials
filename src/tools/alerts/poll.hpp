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

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/* The chat poll at /enquete: the web panel opens it with a question and 2 to
 * 6 options, people vote with "!voto N" (or "!vote N") on any of the three
 * chats, one vote per person per platform, and they may change it. Pure
 * logic, so the tests run without OBS. */
namespace Poll {

constexpr int kMinOptions = 2;
constexpr int kMaxOptions = 6;
constexpr int kMaxSeconds = 3600;

/* The look and the choices that are not a poll: {font, fontSize, textColor,
 * accent, bubbleColor, bubbleOpacity, shadow, resultSeconds, announce}. */
QJsonObject normalizeConfig(const QJsonObject &stored);

/* "!voto 2", "!Vote 2 please" -> 2; anything else -> 0. */
int voteFromChat(const QString &text);

class Session {
public:
	/* Empty when it opened, else what was wrong: "question", "options" or
	 * "duration". seconds 0 = until closed by hand. */
	QString start(const QString &question, const QStringList &options, int seconds, qint64 nowMs);
	/* Closes it by hand; false when it was not open. */
	bool stop(qint64 nowMs);
	/* Closes it when the time is up; true when it just closed. */
	bool expire(qint64 nowMs);
	/* option is 1-based. voter is "platform:user". True when it counted (a
	 * new vote or a changed one). */
	bool vote(const QString &voter, int option, qint64 nowMs);

	bool isOpen() const { return m_open; }
	bool exists() const { return !m_options.isEmpty(); }
	QString question() const { return m_question; }
	QStringList options() const { return m_options; }
	QList<int> counts() const;
	/* 1-based; 0 when nobody voted. Ties go to the first. */
	int winner() const;
	/* {question, options:[{text, votes}], total, open, remainingMs, closedAt,
	 * winner}; empty object when there has never been a poll. */
	QJsonObject toJson(qint64 nowMs) const;

	QJsonObject save() const;
	void load(const QJsonObject &stored);

private:
	QString m_question;
	QStringList m_options;
	QHash<QString, int> m_votes; /* voter -> 0-based option */
	bool m_open = false;
	qint64 m_endsAt = 0; /* 0 = no time limit */
	qint64 m_closedAt = 0;
};

} // namespace Poll
