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

#include "twitch-chat.hpp"

#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {

QString unescapeTag(const QByteArray &raw)
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

} // namespace

TwitchChat::TwitchChat(QNetworkAccessManager *net, QObject *parent) : ChatConnector(ChatPlatform::Twitch, net, parent)
{
	connect(&m_ws, &WsClient::opened, this, [this]() {
		const QByteArray nick =
			"justinfan" + QByteArray::number(QRandomGenerator::global()->bounded(10000, 99999));
		m_ws.sendText("CAP REQ :twitch.tv/tags twitch.tv/commands");
		m_ws.sendText("PASS SCHMOOPIIE");
		m_ws.sendText("NICK " + nick);
		m_ws.sendText("JOIN #" + m_channel.toUtf8());
	});
	connect(&m_ws, &WsClient::textReceived, this, [this](const QByteArray &data) {
		for (const QByteArray &line : data.split('\n')) {
			const QByteArray trimmed = line.trimmed();
			if (!trimmed.isEmpty())
				handleLine(trimmed);
		}
	});
	connect(&m_ws, &WsClient::closed, this, [this](const QString &reason) {
		setState(ConnectorState::Error, reason);
		scheduleReconnect();
	});
}

QString TwitchChat::normalizeChannel(const QString &input)
{
	QString s = input.trimmed().toLower();
	static const QRegularExpression urlRe(QStringLiteral("twitch\\.tv/([a-z0-9_]+)"));
	const auto m = urlRe.match(s);
	if (m.hasMatch())
		return m.captured(1);
	s.remove(QLatin1Char('#'));
	s.remove(QLatin1Char('@'));
	return s;
}

void TwitchChat::connectNow()
{
	m_channel = normalizeChannel(target());
	setState(ConnectorState::Connecting);
	m_ws.open(QUrl(QStringLiteral("wss://irc-ws.chat.twitch.tv:443")));
}

void TwitchChat::disconnectNow()
{
	m_ws.close();
}

void TwitchChat::handleLine(const QByteArray &line)
{
	QByteArray rest = line;
	QHash<QByteArray, QByteArray> tags;

	if (rest.startsWith('@')) {
		const qsizetype sp = rest.indexOf(' ');
		if (sp < 0)
			return;
		for (const QByteArray &kv : rest.mid(1, sp - 1).split(';')) {
			const qsizetype eq = kv.indexOf('=');
			if (eq > 0)
				tags.insert(kv.left(eq), kv.mid(eq + 1));
		}
		rest = rest.mid(sp + 1);
	}

	QByteArray prefix;
	if (rest.startsWith(':')) {
		const qsizetype sp = rest.indexOf(' ');
		if (sp < 0)
			return;
		prefix = rest.mid(1, sp - 1);
		rest = rest.mid(sp + 1);
	}

	const qsizetype sp = rest.indexOf(' ');
	const QByteArray command = sp < 0 ? rest : rest.left(sp);
	const QByteArray params = sp < 0 ? QByteArray() : rest.mid(sp + 1);

	if (command == "PING") {
		m_ws.sendText("PONG " + params);
	} else if (command == "366" || command == "ROOMSTATE") {
		markHealthy();
	} else if (command == "RECONNECT") {
		scheduleRetry(1);
	} else if (command == "NOTICE" && params.contains("Login")) {
		setState(ConnectorState::Error, QString::fromUtf8(params.mid(params.indexOf(':') + 1)));
	} else if (command == "PRIVMSG") {
		const qsizetype colon = params.indexOf(" :");
		if (colon < 0)
			return;
		QString text = QString::fromUtf8(params.mid(colon + 2));
		if (text.startsWith(QStringLiteral("\x01"
						   "ACTION ")) &&
		    text.endsWith(QLatin1Char('\x01')))
			text = text.mid(8, text.size() - 9);

		QString author = unescapeTag(tags.value("display-name"));
		if (author.isEmpty())
			author = QString::fromUtf8(prefix.left(prefix.indexOf('!')));

		emitMessage(author, QString::fromUtf8(tags.value("color")), text);
	}
}
