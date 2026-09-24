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

#include "tiktok-chat.hpp"

#include "tiktok-proto.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {

constexpr int kOfflineRecheckSeconds = 60;
constexpr int kLiveStatus = 2;

QByteArray encodeQuery(const QList<QPair<QByteArray, QByteArray>> &params)
{
	QByteArray out;
	for (const auto &p : params) {
		if (!out.isEmpty())
			out += '&';
		out += QUrl::toPercentEncoding(QString::fromUtf8(p.first)) + '=' +
		       QUrl::toPercentEncoding(QString::fromUtf8(p.second));
	}
	return out;
}

} // namespace

TikTokChat::TikTokChat(QNetworkAccessManager *net, QObject *parent) : ChatConnector(ChatPlatform::TikTok, net, parent)
{
	m_heartbeat.setInterval(10000);
	connect(&m_heartbeat, &QTimer::timeout, this, &TikTokChat::sendHeartbeat);

	connect(&m_ws, &WsClient::opened, this, [this]() {
		QByteArray enter;
		putVarintField(enter, 1, m_roomId.toULongLong());
		putVarintField(enter, 4, 12);
		putBytesField(enter, 5, "audience");
		putVarintField(enter, 8, QRandomGenerator::global()->generate64() & 0x7FFFFFFFFFFFFFFFULL);
		putBytesField(enter, 9, "0");
		m_ws.sendBinary(pushFrame("im_enter_room", enter));
		m_seq = 1;
		m_heartbeat.start();
		markHealthy();
	});
	connect(&m_ws, &WsClient::binaryReceived, this, &TikTokChat::onFrame);
	connect(&m_ws, &WsClient::closed, this, [this](const QString &reason) {
		m_heartbeat.stop();
		setState(ConnectorState::Error, reason);
		scheduleReconnect();
	});
}

QString TikTokChat::normalizeUser(const QString &input)
{
	QString s = input.trimmed();
	static const QRegularExpression urlRe(QStringLiteral("tiktok\\.com/@([^/?#]+)"));
	const auto m = urlRe.match(s);
	if (m.hasMatch())
		return m.captured(1);
	s.remove(QLatin1Char('@'));
	return s;
}

QNetworkReply *TikTokChat::get(const QUrl &url)
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	req.setRawHeader("Accept", "*/*");
	req.setTransferTimeout(20000);
	m_pending = net()->get(req);
	return m_pending;
}

void TikTokChat::connectNow()
{
	setState(ConnectorState::Connecting);
	const QString user = QString::fromUtf8(QUrl::toPercentEncoding(normalizeUser(target())));
	QNetworkReply *reply =
		get(QUrl(QStringLiteral("https://www.tiktok.com/api-live/user/room/?aid=1988&sourceType=54&uniqueId=%1")
				 .arg(user)));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onRoomInfo(reply); });
}

void TikTokChat::disconnectNow()
{
	m_heartbeat.stop();
	if (m_pending) {
		m_pending->abort();
		m_pending = nullptr;
	}
	m_ws.close();
}

void TikTokChat::onRoomInfo(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	if (reply->error() != QNetworkReply::NoError) {
		setState(ConnectorState::Error, reply->errorString());
		scheduleReconnect();
		return;
	}

	const QJsonObject data =
		QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("data")).toObject();
	m_roomId = data.value(QStringLiteral("user")).toObject().value(QStringLiteral("roomId")).toString();
	const int status = data.value(QStringLiteral("liveRoom")).toObject().value(QStringLiteral("status")).toInt();

	if (m_roomId.isEmpty() && data.isEmpty()) {
		setState(ConnectorState::Error, QStringLiteral("user not found"));
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}
	if (m_roomId.isEmpty() || status != kLiveStatus) {
		setState(ConnectorState::Offline);
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}

	const QString ua = QString::fromUtf8(QUrl::toPercentEncoding(QString::fromLatin1(kBrowserUserAgent)));
	QNetworkReply *signReply =
		get(QUrl(QStringLiteral("https://tiktok.eulerstream.com/webcast/fetch?client=meketreve-obs-essentials"
					"&room_id=%1&user_agent=%2")
				 .arg(m_roomId, ua)));
	connect(signReply, &QNetworkReply::finished, this, [this, signReply]() { onSignedFetch(signReply); });
}

void TikTokChat::onSignedFetch(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const QByteArray cookie = reply->rawHeader("x-set-tt-cookie");
	const QByteArray body = reply->readAll();

	if (status == 429) {
		setState(ConnectorState::Error, QStringLiteral("sign server rate limit"));
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}
	if (reply->error() != QNetworkReply::NoError || status != 200) {
		setState(ConnectorState::Error, QStringLiteral("sign server: %1").arg(reply->errorString()));
		scheduleReconnect();
		return;
	}

	const FetchResult fetch = parseFetchResult(body);
	if (fetch.pushServer.isEmpty() || fetch.cursor.isEmpty()) {
		setState(ConnectorState::Error, QStringLiteral("sign server returned no WebSocket URL"));
		scheduleReconnect();
		return;
	}

	const QByteArray browserVersion = QByteArray(kBrowserUserAgent).mid(8);
	QList<QPair<QByteArray, QByteArray>> params{
		{"version_code", "180800"},
		{"aid", "1988"},
		{"app_language", "en-US"},
		{"app_name", "tiktok_web"},
		{"browser_platform", "Win32"},
		{"browser_language", "en-US"},
		{"browser_name", "Mozilla"},
		{"browser_version", browserVersion},
		{"browser_online", "true"},
		{"cookie_enabled", "true"},
		{"tz_name", "Etc/UTC"},
		{"device_platform", "web"},
		{"identity", "audience"},
		{"live_id", "12"},
		{"webcast_language", "en"},
		{"ws_direct", "0"},
		{"sup_ws_ds_opt", "1"},
		{"update_version_code", "2.0.0"},
		{"did_rule", "3"},
		{"screen_height", "1080"},
		{"screen_width", "1920"},
		{"heartbeat_duration", "0"},
		{"resp_content_type", "protobuf"},
		{"history_comment_count", "6"},
		{"client_enter", "1"},
		{"last_rtt", QByteArray::number(QRandomGenerator::global()->bounded(100, 200))},
		{"room_id", m_roomId.toUtf8()},
		{"internal_ext", fetch.internalExt},
		{"cursor", fetch.cursor},
	};
	params += fetch.routeParams;

	const QByteArray url = fetch.pushServer + '?' + encodeQuery(params) + "&version_code=270000";
	m_ws.open(QUrl::fromEncoded(url, QUrl::StrictMode),
		  {{"User-Agent", kBrowserUserAgent}, {"Cookie", cookie}, {"Origin", "https://www.tiktok.com"}});
}

void TikTokChat::sendHeartbeat()
{
	QByteArray hb;
	putVarintField(hb, 1, m_roomId.toULongLong());
	putVarintField(hb, 2, m_seq++);
	m_ws.sendBinary(pushFrame("hb", hb));
}

void TikTokChat::onFrame(const QByteArray &data)
{
	quint64 logId = 0;
	QByteArray type, payload;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		if (f.number == 2)
			logId = f.varint;
		else if (f.number == 7)
			type = f.bytes;
		else if (f.number == 8)
			payload = f.bytes;
	}

	/* We never ask for compress=gzip, so a gzip payload means the protocol
	 * changed under us; drop it rather than feed garbage to the parser. */
	if (type != "msg" || payload.startsWith("\x1f\x8b"))
		return;

	const FetchResult result = parseFetchResult(payload);
	if (result.needAck && logId)
		m_ws.sendBinary(pushFrame("ack", result.internalExt, logId));

	for (const auto &msg : result.messages) {
		const QByteArray &method = msg.first;
		if (method == "WebcastChatMessage") {
			TikTokChatMessage chat;
			if (parseTikTokChat(msg.second, chat))
				emitMessage(chat.user.displayName(), QString(), chat.text);
		} else if (method == "WebcastGiftMessage") {
			TikTokGift gift;
			if (parseTikTokGift(msg.second, gift) && gift.isFinal()) {
				ChatMessage ev{ChatPlatform::TikTok, gift.user.displayName(), QString(), QString(),
					       QString()};
				ev.event = ChatEvent::Gift;
				ev.amount = gift.repeatCount;
				ev.detail = gift.diamonds > 0 ? QStringLiteral("%1 (%2 \u2666)")
									.arg(gift.name)
									.arg(gift.diamonds * gift.repeatCount)
							      : gift.name;
				emitFull(ev);
			}
		} else if (method == "WebcastSocialMessage") {
			TikTokSocial social;
			if (!parseTikTokSocial(msg.second, social))
				continue;
			if (social.displayKey.contains(QLatin1String("follow")))
				emitEvent(ChatEvent::Follow, social.user.displayName());
			else if (social.displayKey.contains(QLatin1String("share")))
				emitEvent(ChatEvent::Share, social.user.displayName());
		} else if (method == "WebcastLikeMessage") {
			TikTokLike like;
			if (parseTikTokLike(msg.second, like))
				emitEvent(ChatEvent::Like, like.user.displayName(), like.count);
		}
	}
}
