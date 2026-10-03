/*
Meketreve OBS Essentials
Copyright (C) 2026 Meketreve

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

#include "trovo-chat.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUuid>

const char *const kTrovoClientId = "0cc6ed2a7a24ba10d1d212a5916250c7";

namespace {

const char *const kApiBase = "https://open-api.trovo.live/openplatform/";
const char *const kChatUrl = "wss://open-chat.trovo.live/chat";
constexpr int kDefaultPingSeconds = 30;

/* Chat service message types. */
enum TrovoType {
	kChat = 0,
	kSpell = 5,
	kSuperCap = 6,
	kColorful = 7,
	kSpellChat = 8,
	kBulletScreen = 9,
	kSubscription = 5001,
	kFollow = 5003,
	kGiftSubRandom = 5005,
	kGiftSubUser = 5006,
	kRaid = 5008,
	kCustomSpell = 5009,
};

int firstNumber(const QString &text)
{
	static const QRegularExpression re(QStringLiteral("\\d+"));
	const QRegularExpressionMatch m = re.match(text);
	return m.hasMatch() ? m.captured().toInt() : 0;
}

QString nonce()
{
	return QUuid::createUuid().toString(QUuid::Id128);
}

} // namespace

TrovoChat::TrovoChat(QNetworkAccessManager *net, QObject *parent) : ChatConnector(ChatPlatform::Trovo, net, parent)
{
	connect(&m_ws, &WsClient::opened, this, [this]() {
		m_authNonce = nonce();
		m_connectedAt = QDateTime::currentSecsSinceEpoch();
		m_ws.sendText(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("AUTH")},
							{QStringLiteral("nonce"), m_authNonce},
							{QStringLiteral("data"),
							 QJsonObject{{QStringLiteral("token"), m_token}}}})
				      .toJson(QJsonDocument::Compact));
	});
	connect(&m_ws, &WsClient::textReceived, this, &TrovoChat::handleFrame);
	connect(&m_ws, &WsClient::closed, this, [this](const QString &reason) {
		setState(ConnectorState::Error, reason);
		scheduleReconnect();
	});
	m_pingTimer.setInterval(kDefaultPingSeconds * 1000);
	connect(&m_pingTimer, &QTimer::timeout, this, &TrovoChat::ping);
}

QString TrovoChat::normalizeChannel(const QString &input)
{
	QString s = input.trimmed().toLower();
	static const QRegularExpression urlRe(QStringLiteral("trovo\\.live/(?:s/)?([a-z0-9_]+)"));
	const auto m = urlRe.match(s);
	if (m.hasMatch())
		return m.captured(1);
	s.remove(QLatin1Char('@'));
	return s;
}

QNetworkReply *TrovoChat::api(const QString &path, const QJsonObject *body)
{
	QNetworkRequest req(QUrl(QString::fromLatin1(kApiBase) + path));
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	req.setRawHeader("Accept", "application/json");
	req.setRawHeader("Client-ID", kTrovoClientId);
	req.setTransferTimeout(15000);
	if (body) {
		req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		m_pending = net()->post(req, QJsonDocument(*body).toJson(QJsonDocument::Compact));
	} else {
		m_pending = net()->get(req);
	}
	return m_pending;
}

void TrovoChat::connectNow()
{
	setState(ConnectorState::Connecting);
	const QString channel = normalizeChannel(target());
	bool numeric = false;
	channel.toLongLong(&numeric);
	if (numeric) {
		m_channelId = channel;
		requestToken();
		return;
	}
	const QJsonObject body{{QStringLiteral("user"), QJsonArray{channel}}};
	QNetworkReply *reply = api(QStringLiteral("getusers"), &body);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onUsers(reply); });
}

void TrovoChat::disconnectNow()
{
	m_pingTimer.stop();
	if (m_pending) {
		m_pending->abort();
		m_pending = nullptr;
	}
	m_ws.close();
}

void TrovoChat::onUsers(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;
	const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
	const QString id = root.value(QStringLiteral("users"))
				   .toArray()
				   .at(0)
				   .toObject()
				   .value(QStringLiteral("channel_id"))
				   .toString();
	if (id.isEmpty()) {
		/* An unknown name comes back as an error, not an empty list. */
		const bool network = reply->error() != QNetworkReply::NoError &&
				     reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0;
		setState(ConnectorState::Error, network ? reply->errorString() : QStringLiteral("channel not found"));
		if (network)
			scheduleReconnect();
		else
			scheduleRetry(60);
		return;
	}
	m_channelId = id;
	requestToken();
}

void TrovoChat::requestToken()
{
	QNetworkReply *reply = api(QStringLiteral("chat/channel-token/") + m_channelId);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onToken(reply); });
}

void TrovoChat::onToken(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;
	m_token = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("token")).toString();
	if (m_token.isEmpty()) {
		setState(ConnectorState::Error, reply->error() != QNetworkReply::NoError
							? reply->errorString()
							: QStringLiteral("no chat token"));
		scheduleReconnect();
		return;
	}
	/* The token is good for 20 seconds: connect right away. */
	m_ws.open(QUrl(QString::fromLatin1(kChatUrl)));
}

void TrovoChat::ping()
{
	m_ws.sendText(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("PING")},
						{QStringLiteral("nonce"), nonce()}})
			      .toJson(QJsonDocument::Compact));
}

void TrovoChat::handleFrame(const QByteArray &data)
{
	const QJsonObject frame = QJsonDocument::fromJson(data).object();
	const QString type = frame.value(QStringLiteral("type")).toString();
	if (type == QLatin1String("RESPONSE")) {
		if (frame.value(QStringLiteral("nonce")).toString() != m_authNonce)
			return;
		const QString error = frame.value(QStringLiteral("error")).toString();
		if (!error.isEmpty()) {
			setState(ConnectorState::Error, error);
			scheduleReconnect();
			return;
		}
		markHealthy();
		m_pingTimer.start();
	} else if (type == QLatin1String("PONG")) {
		/* The service says how often it wants to hear from us. */
		const int gap = frame.value(QStringLiteral("data")).toObject().value(QStringLiteral("gap")).toInt();
		if (gap > 0 && gap * 1000 != m_pingTimer.interval())
			m_pingTimer.setInterval(gap * 1000);
	} else if (type == QLatin1String("CHAT")) {
		for (const QJsonValue chat :
		     frame.value(QStringLiteral("data")).toObject().value(QStringLiteral("chats")).toArray())
			handleChat(chat.toObject());
	}
}

void TrovoChat::handleChat(const QJsonObject &chat)
{
	/* The first frame after connecting replays recent history. */
	const qint64 sent = chat.value(QStringLiteral("send_time")).toInteger();
	if (m_connectedAt > 0 && sent > 0 && sent < m_connectedAt)
		return;

	const int type = chat.value(QStringLiteral("type")).toInt(-1);
	const QString nick = chat.value(QStringLiteral("nick_name")).toString();
	const QString content = chat.value(QStringLiteral("content")).toString();

	switch (type) {
	case kChat:
	case kSuperCap:
	case kColorful:
	case kSpellChat:
	case kBulletScreen: {
		ChatMessage msg;
		msg.author = nick;
		msg.text = content;
		/* Moderation commands take the user name; deleting a message
		 * takes its id and the sender's id together. */
		msg.userId = chat.value(QStringLiteral("user_name")).toString();
		const QString messageId = chat.value(QStringLiteral("message_id")).toString();
		if (!messageId.isEmpty())
			msg.id = messageId + QLatin1Char('|') +
				 QString::number(chat.value(QStringLiteral("sender_id")).toInteger());
		msg.channelId = m_channelId;
		for (const QJsonValue role : chat.value(QStringLiteral("roles")).toArray()) {
			const QString r = role.toString().toLower();
			msg.isBroadcaster |= r == QLatin1String("streamer");
			msg.isMod |= r == QLatin1String("mod") || r == QLatin1String("supermod");
			msg.isSub |= r == QLatin1String("subscriber");
		}
		emitFull(msg);
		break;
	}
	case kSpell:
	case kCustomSpell: {
		/* content is JSON: {"gift": name, "num": count, "gift_value": ...}. */
		const QJsonObject spell = QJsonDocument::fromJson(content.toUtf8()).object();
		const QString gift = spell.value(QStringLiteral("gift")).toString();
		const int num = spell.value(QStringLiteral("num")).toInt(1);
		emitEvent(ChatEvent::Gift, nick, std::max(1, num), gift.isEmpty() ? content : gift);
		break;
	}
	case kSubscription: {
		const QString tier = chat.value(QStringLiteral("sub_tier")).toVariant().toString();
		emitEvent(ChatEvent::Sub, nick, 1, tier.isEmpty() ? QString() : QStringLiteral("Tier ") + tier);
		break;
	}
	case kFollow:
		emitEvent(ChatEvent::Follow, nick);
		break;
	case kGiftSubRandom: /* content: how many */
		emitEvent(ChatEvent::GiftSub, nick, std::max(1, firstNumber(content)));
		break;
	case kGiftSubUser: /* content: "<user id>,<recipient>" */
		emitEvent(ChatEvent::GiftSub, nick, 1, content.section(QLatin1Char(','), 1));
		break;
	case kRaid: {
		/* "<nick> is carrying <n> raiders to this channel. Welcome!" */
		static const QRegularExpression raiders(QStringLiteral("carrying (\\d+)"));
		const QRegularExpressionMatch m = raiders.match(content);
		emitEvent(ChatEvent::Raid, nick, m.hasMatch() ? m.captured(1).toInt() : firstNumber(content));
		break;
	}
	default:
		break; /* joins, unfollows, stream on/off, activity */
	}
}
