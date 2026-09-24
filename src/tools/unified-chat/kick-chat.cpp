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

#include "kick-chat.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <algorithm>

namespace {

const char *const kPusherUrl =
	"wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0&flash=false";

QString stripEmotes(const QString &text)
{
	static const QRegularExpression emoteRe(QStringLiteral("\\[emote:\\d+:([^\\]]+)\\]"));
	QString out = text;
	out.replace(emoteRe, QStringLiteral("\\1"));
	return out;
}

} // namespace

KickChat::KickChat(QNetworkAccessManager *net, QObject *parent) : ChatConnector(ChatPlatform::Kick, net, parent)
{
	connect(&m_ws, &WsClient::textReceived, this, &KickChat::handleEvent);
	connect(&m_ws, &WsClient::closed, this, [this](const QString &reason) {
		setState(ConnectorState::Error, reason);
		scheduleReconnect();
	});
}

QString KickChat::normalizeChannel(const QString &input)
{
	QString s = input.trimmed().toLower();
	static const QRegularExpression urlRe(QStringLiteral("kick\\.com/([a-z0-9_-]+)"));
	const auto m = urlRe.match(s);
	if (m.hasMatch())
		return m.captured(1);
	s.remove(QLatin1Char('@'));
	return s;
}

void KickChat::connectNow()
{
	const QString channel = normalizeChannel(target());
	setState(ConnectorState::Connecting);

	bool numeric = false;
	channel.toLongLong(&numeric);
	if (numeric) {
		m_chatroomId = channel;
		m_channelId.clear();
		m_ws.open(QUrl(QString::fromLatin1(kPusherUrl)));
		return;
	}

	QNetworkRequest req(QUrl(QStringLiteral("https://kick.com/api/v2/channels/%1").arg(channel)));
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	req.setRawHeader("Accept", "application/json");
	req.setTransferTimeout(15000);
	m_pending = net()->get(req);
	QNetworkReply *reply = m_pending;
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onChannelInfo(reply); });
}

void KickChat::disconnectNow()
{
	if (m_pending) {
		m_pending->abort();
		m_pending = nullptr;
	}
	m_ws.close();
}

void KickChat::onChannelInfo(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (status == 404) {
		setState(ConnectorState::Error, QStringLiteral("channel not found"));
		scheduleRetry(60);
		return;
	}

	const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
	const QJsonValue id = root.value(QStringLiteral("chatroom")).toObject().value(QStringLiteral("id"));
	if (reply->error() != QNetworkReply::NoError || id.isUndefined()) {
		/* Cloudflare sometimes blocks the API; the numeric chatroom id can be
		 * typed in the settings instead of the channel name. */
		setState(ConnectorState::Error, reply->error() != QNetworkReply::NoError
							? reply->errorString()
							: QStringLiteral("HTTP %1").arg(status));
		scheduleReconnect();
		return;
	}

	m_chatroomId = QString::number(id.toInteger());
	/* Follows and some channel events come on channel.<id>, not the chatroom. */
	const QJsonValue channelId = root.value(QStringLiteral("id"));
	m_channelId = channelId.isUndefined() ? QString() : QString::number(channelId.toInteger());
	m_ws.open(QUrl(QString::fromLatin1(kPusherUrl)));
}

void KickChat::subscribe(const QString &channel)
{
	const QJsonObject sub{{QStringLiteral("event"), QStringLiteral("pusher:subscribe")},
			      {QStringLiteral("data"),
			       QJsonObject{{QStringLiteral("auth"), QString()}, {QStringLiteral("channel"), channel}}}};
	m_ws.sendText(QJsonDocument(sub).toJson(QJsonDocument::Compact));
}

void KickChat::handleEvent(const QByteArray &data)
{
	const QJsonObject msg = QJsonDocument::fromJson(data).object();
	const QString event = msg.value(QStringLiteral("event")).toString();
	/* Pusher double-encodes: "data" is a JSON string. */
	const QJsonObject payload =
		QJsonDocument::fromJson(msg.value(QStringLiteral("data")).toString().toUtf8()).object();
	const auto str = [&payload](const char *key) {
		return payload.value(QLatin1String(key)).toString();
	};
	const auto num = [&payload](const char *key) {
		return payload.value(QLatin1String(key)).toInt();
	};

	if (event == QLatin1String("pusher:connection_established")) {
		subscribe(QStringLiteral("chatrooms.%1.v2").arg(m_chatroomId));
		if (!m_channelId.isEmpty()) {
			subscribe(QStringLiteral("channel.%1").arg(m_channelId));
			/* The legacy channel carries the activity feed (follows). */
			subscribe(QStringLiteral("channel_%1").arg(m_channelId));
		}
	} else if (event == QLatin1String("pusher_internal:subscription_succeeded")) {
		markHealthy();
	} else if (event == QLatin1String("pusher:ping")) {
		m_ws.sendText(R"({"event":"pusher:pong","data":{}})");
	} else if (event == QLatin1String("pusher:error")) {
		setState(ConnectorState::Error,
			 msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString());
	} else if (event == QLatin1String("App\\Events\\ChatMessageEvent")) {
		const QJsonObject sender = payload.value(QStringLiteral("sender")).toObject();
		ChatMessage chat{
			ChatPlatform::Kick, sender.value(QStringLiteral("username")).toString(),
			sender.value(QStringLiteral("identity")).toObject().value(QStringLiteral("color")).toString(),
			stripEmotes(str("content")), QString()};
		chat.id = str("id");
		chat.userId = QString::number(sender.value(QStringLiteral("id")).toInteger());
		emitFull(chat);
	} else if (event == QLatin1String("App\\Events\\SubscriptionEvent")) {
		emitEvent(ChatEvent::Sub, str("username"), std::max(1, num("months")));
	} else if (event == QLatin1String("App\\Events\\GiftedSubscriptionsEvent")) {
		const QJsonArray to = payload.value(QStringLiteral("gifted_usernames")).toArray();
		emitEvent(ChatEvent::GiftSub, str("gifter_username"), std::max(1, static_cast<int>(to.size())),
			  to.size() == 1 ? to.at(0).toString() : QString());
	} else if (event == QLatin1String("App\\Events\\StreamHostEvent")) {
		emitEvent(ChatEvent::Raid, str("host_username"), num("number_viewers"), QString(),
			  str("optional_message"));
	} else if (event == QLatin1String("App\\Events\\FollowersUpdated")) {
		/* Also sent on unfollow, and without a name for anonymous updates. */
		if (payload.value(QStringLiteral("followed")).toBool() && !str("username").isEmpty())
			emitEvent(ChatEvent::Follow, str("username"));
	} else if (event == QLatin1String("App\\Events\\NewActivityFeedEvent")) {
		/* Seen live with type "new_subscriber" (already covered by
		 * SubscriptionEvent); follows are taken from here too. */
		if (str("type").contains(QLatin1String("follow")) && !str("username").isEmpty())
			emitEvent(ChatEvent::Follow, str("username"));
	} else if (event.endsWith(QLatin1String("KicksGifted"))) {
		/* Kick's paid "Kicks": not seen live yet, parsed defensively. */
		const QJsonObject sender = payload.value(QStringLiteral("sender")).toObject();
		const QJsonObject gift = payload.value(QStringLiteral("gift")).toObject();
		emitEvent(ChatEvent::Gift, sender.value(QStringLiteral("username")).toString(),
			  gift.value(QStringLiteral("amount")).toInt(), gift.value(QStringLiteral("name")).toString(),
			  str("message"));
	}
}
