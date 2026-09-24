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

#include "irc-message.hpp"

QString unescapeIrcTag(const QByteArray &raw)
{
	QString out;
	const QString in = QString::fromUtf8(raw);
	for (qsizetype i = 0; i < in.size(); i++) {
		if (in[i] != QLatin1Char('\\') || i + 1 >= in.size()) {
			out += in[i];
			continue;
		}
		const QChar next = in[++i];
		if (next == QLatin1Char('s'))
			out += QLatin1Char(' ');
		else if (next == QLatin1Char(':'))
			out += QLatin1Char(';');
		else if (next == QLatin1Char('r') || next == QLatin1Char('n'))
			continue;
		else
			out += next;
	}
	return out;
}

QString IrcMessage::tag(const char *key) const
{
	return unescapeIrcTag(tags.value(key));
}

QByteArray IrcMessage::trailing() const
{
	if (params.startsWith(':'))
		return params.mid(1);
	const qsizetype colon = params.indexOf(" :");
	return colon < 0 ? QByteArray() : params.mid(colon + 2);
}

QByteArray IrcMessage::nick() const
{
	const qsizetype bang = prefix.indexOf('!');
	return bang < 0 ? prefix : prefix.left(bang);
}

bool parseIrcLine(const QByteArray &line, IrcMessage &out)
{
	out = IrcMessage();
	QByteArray rest = line;

	if (rest.startsWith('@')) {
		const qsizetype sp = rest.indexOf(' ');
		if (sp < 0)
			return false;
		for (const QByteArray &kv : rest.mid(1, sp - 1).split(';')) {
			const qsizetype eq = kv.indexOf('=');
			if (eq > 0)
				out.tags.insert(kv.left(eq), kv.mid(eq + 1));
			else if (!kv.isEmpty())
				out.tags.insert(kv, QByteArray());
		}
		rest = rest.mid(sp + 1);
	}

	if (rest.startsWith(':')) {
		const qsizetype sp = rest.indexOf(' ');
		if (sp < 0)
			return false;
		out.prefix = rest.mid(1, sp - 1);
		rest = rest.mid(sp + 1);
	}

	const qsizetype sp = rest.indexOf(' ');
	out.command = sp < 0 ? rest : rest.left(sp);
	out.params = sp < 0 ? QByteArray() : rest.mid(sp + 1);
	return !out.command.isEmpty();
}
