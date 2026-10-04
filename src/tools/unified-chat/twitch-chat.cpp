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

#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

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

void TwitchChat::fetchViewers()
{
	if (!m_viewerLookup || m_channel.isEmpty())
		return;
	QPointer<TwitchChat> self(this);
	const QString channel = m_channel;
	m_viewerLookup(channel, [self, channel](int viewers) {
		if (self && self->running() && self->m_channel == channel)
			self->setViewers(viewers);
	});
}

QList<ChatEmote> TwitchChat::parseEmotes(const QString &tag, const QString &text)
{
	/* UTF-16 offset of every code point. */
	QList<qsizetype> offsets;
	for (qsizetype i = 0; i < text.size(); i++) {
		offsets.append(i);
		if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate())
			i++;
	}
	offsets.append(text.size());

	QList<ChatEmote> out;
	for (const QString &emote : tag.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
		const qsizetype colon = emote.indexOf(QLatin1Char(':'));
		if (colon <= 0)
			continue;
		const QString id = emote.left(colon);
		for (const QString &range : emote.mid(colon + 1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			bool okA = false, okB = false;
			const qsizetype from = range.section(QLatin1Char('-'), 0, 0).toLongLong(&okA);
			const qsizetype to = range.section(QLatin1Char('-'), 1, 1).toLongLong(&okB);
			if (!okA || !okB || from < 0 || to < from || to + 1 >= offsets.size())
				continue;
			ChatEmote e;
			e.start = offsets[from];
			e.length = offsets[to + 1] - e.start;
			e.url = QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/static/dark/2.0").arg(id);
			out.append(e);
		}
	}
	std::sort(out.begin(), out.end(), [](const ChatEmote &a, const ChatEmote &b) { return a.start < b.start; });
	return out;
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

		ChatMessage msg{ChatPlatform::Twitch, author, QString::fromUtf8(irc.tags.value("color")), text,
				QString()};
		msg.id = irc.tag("id");
		msg.userId = irc.tag("user-id");
		msg.channelId = irc.tag("room-id");
		msg.emotes = parseEmotes(irc.tag("emotes"), text);
		const QString badges = irc.tag("badges");
		msg.isBroadcaster = badges.contains(QLatin1String("broadcaster/"));
		msg.isMod = irc.tag("mod") == QLatin1String("1") || badges.contains(QLatin1String("moderator/"));
		msg.isSub = irc.tag("subscriber") == QLatin1String("1") ||
			    badges.contains(QLatin1String("subscriber/")) || badges.contains(QLatin1String("founder/"));
		msg.isReply = irc.tags.contains("reply-parent-msg-id");
		const int bits = irc.tag("bits").toInt();
		if (bits > 0) {
			msg.event = ChatEvent::Bits;
			msg.amount = bits;
		}
		emitFull(msg);
	} else if (command == "USERNOTICE") {
		handleUserNotice(irc);
	} else if (command == "CLEARMSG") {
		emitRemoval(irc.tag("target-msg-id"), QString());
	} else if (command == "CLEARCHAT") {
		/* A user after the channel: timeout or ban; none: the chat was cleared. */
		const QString user = irc.tag("target-user-id");
		if (user.isEmpty())
			emitRemoval(QString(), QString(), true);
		else
			emitRemoval(QString(), user);
	}
}

void TwitchChat::handleUserNotice(const IrcMessage &irc)
{
	const QString kind = irc.tag("msg-id");
	QString author = irc.tag("display-name");
	if (author.isEmpty())
		author = irc.tag("login");

	ChatMessage msg{ChatPlatform::Twitch, author, QString::fromUtf8(irc.tags.value("color")),
			QString::fromUtf8(irc.trailing()), QString()};
	msg.id = irc.tag("id");
	msg.userId = irc.tag("user-id");

	const QString plan = irc.tag("msg-param-sub-plan");
	const QString tier = plan == QLatin1String("Prime") ? QStringLiteral("Prime")
			     : plan.size() == 4             ? QStringLiteral("Tier %1").arg(plan.left(1))
							    : QString();

	if (kind == QLatin1String("sub") || kind == QLatin1String("resub")) {
		msg.event = ChatEvent::Sub;
		msg.amount = std::max(1, irc.tag("msg-param-cumulative-months").toInt());
		msg.detail = tier;
	} else if (kind == QLatin1String("subgift")) {
		msg.event = ChatEvent::GiftSub;
		msg.amount = 1;
		msg.detail = irc.tag("msg-param-recipient-display-name");
	} else if (kind == QLatin1String("submysterygift")) {
		msg.event = ChatEvent::GiftSub;
		msg.amount = std::max(1, irc.tag("msg-param-mass-gift-count").toInt());
		msg.detail = tier;
	} else if (kind == QLatin1String("raid")) {
		msg.event = ChatEvent::Raid;
		msg.author = irc.tag("msg-param-displayName");
		msg.amount = irc.tag("msg-param-viewerCount").toInt();
	} else {
		return;
	}
	emitFull(msg);
}
