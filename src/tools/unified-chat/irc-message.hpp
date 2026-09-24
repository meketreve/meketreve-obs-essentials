/*
Meketreve OBS Essentials - Unified Chat
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

#include <QByteArray>
#include <QHash>
#include <QString>

/* One IRCv3 line as Twitch sends it: "@tags :prefix COMMAND params :trailing". */
struct IrcMessage {
	QHash<QByteArray, QByteArray> tags;
	QByteArray prefix;
	QByteArray command;
	QByteArray params;

	/* Tag value with IRCv3 escapes (\s, \:, \\) resolved. */
	QString tag(const char *key) const;
	/* Text after " :" in params (the message body), or empty. */
	QByteArray trailing() const;
	/* Nick from "nick!user@host". */
	QByteArray nick() const;
};

QString unescapeIrcTag(const QByteArray &raw);
bool parseIrcLine(const QByteArray &line, IrcMessage &out);
