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

#include "irc-message.hpp"

#include <QRandomGenerator>
#include <QRegularExpression>

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
	IrcMessage irc;
	if (!parseIrcLine(line, irc))
		return;
	const QByteArray &command = irc.command;

	if (command == "PING") {
		m_ws.sendText("PONG " + irc.params);
	} else if (command == "366" || command == "ROOMSTATE") {
		markHealthy();
	} else if (command == "RECONNECT") {
		scheduleRetry(1);
	} else if (command == "NOTICE" && irc.params.contains("Login")) {
		setState(ConnectorState::Error, QString::fromUtf8(irc.trailing()));
	} else if (command == "PRIVMSG") {
		QString text = QString::fromUtf8(irc.trailing());
		if (text.startsWith(QStringLiteral("\x01"
						   "ACTION ")) &&
		    text.endsWith(QLatin1Char('\x01')))
			text = text.mid(8, text.size() - 9);

		QString author = irc.tag("display-name");
		if (author.isEmpty())
			author = QString::fromUtf8(irc.nick());

		emitMessage(author, QString::fromUtf8(irc.tags.value("color")), text);
	}
}
