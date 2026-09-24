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

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {

constexpr int kOfflineRecheckSeconds = 60;
constexpr int kLiveStatus = 2;

/* Just enough protobuf to walk TikTok's webcast messages. Field numbers come
 * from the tiktok-live-proto schema (WebcastPushFrame, ProtoMessageFetchResult,
 * WebcastChatMessage, User). */
struct PbField {
	quint32 number = 0;
	quint8 wire = 0;
	quint64 varint = 0;
	QByteArray bytes;
};

class PbReader {
public:
	explicit PbReader(const QByteArray &data) : m_data(data) {}

	bool next(PbField &f)
	{
		if (m_pos >= m_data.size())
			return false;
		quint64 key = 0;
		if (!readVarint(key))
			return false;
		f.number = static_cast<quint32>(key >> 3);
		f.wire = static_cast<quint8>(key & 7);
		f.bytes.clear();
		f.varint = 0;

		switch (f.wire) {
		case 0:
			return readVarint(f.varint);
		case 1:
			return skip(8);
		case 2: {
			quint64 len = 0;
			if (!readVarint(len) || len > static_cast<quint64>(m_data.size() - m_pos))
				return false;
			f.bytes = m_data.mid(m_pos, static_cast<qsizetype>(len));
			m_pos += static_cast<qsizetype>(len);
			return true;
		}
		case 5:
			return skip(4);
		default:
			return false;
		}
	}

private:
	bool readVarint(quint64 &out)
	{
		out = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (m_pos >= m_data.size())
				return false;
			const auto c = static_cast<quint8>(m_data[m_pos++]);
			out |= static_cast<quint64>(c & 0x7F) << shift;
			if (!(c & 0x80))
				return true;
		}
		return false;
	}

	bool skip(qsizetype n)
	{
		if (m_pos + n > m_data.size())
			return false;
		m_pos += n;
		return true;
	}

	const QByteArray &m_data;
	qsizetype m_pos = 0;
};

void putVarint(QByteArray &out, quint64 v)
{
	while (v >= 0x80) {
		out.append(static_cast<char>((v & 0x7F) | 0x80));
		v >>= 7;
	}
	out.append(static_cast<char>(v));
}

void putVarintField(QByteArray &out, quint32 field, quint64 v)
{
	putVarint(out, static_cast<quint64>(field) << 3);
	putVarint(out, v);
}

void putBytesField(QByteArray &out, quint32 field, const QByteArray &v)
{
	putVarint(out, (static_cast<quint64>(field) << 3) | 2);
	putVarint(out, static_cast<quint64>(v.size()));
	out += v;
}

QByteArray pushFrame(const QByteArray &type, const QByteArray &payload, quint64 logId = 0)
{
	QByteArray out;
	if (logId)
		putVarintField(out, 2, logId);
	putBytesField(out, 6, "pb");
	putBytesField(out, 7, type);
	putBytesField(out, 8, payload);
	return out;
}

struct FetchResult {
	QList<QPair<QByteArray, QByteArray>> messages;
	QList<QPair<QByteArray, QByteArray>> routeParams;
	QByteArray cursor;
	QByteArray internalExt;
	QByteArray pushServer;
	bool needAck = false;
};

FetchResult parseFetchResult(const QByteArray &data)
{
	FetchResult r;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		switch (f.number) {
		case 1: {
			QByteArray method, payload;
			PbReader msg(f.bytes);
			PbField m;
			while (msg.next(m)) {
				if (m.number == 1)
					method = m.bytes;
				else if (m.number == 2)
					payload = m.bytes;
			}
			r.messages.append({method, payload});
			break;
		}
		case 2:
			r.cursor = f.bytes;
			break;
		case 5:
			r.internalExt = f.bytes;
			break;
		case 7: {
			QByteArray key, value;
			PbReader entry(f.bytes);
			PbField e;
			while (entry.next(e)) {
				if (e.number == 1)
					key = e.bytes;
				else if (e.number == 2)
					value = e.bytes;
			}
			if (!value.isEmpty())
				r.routeParams.append({key, value});
			break;
		}
		case 9:
			r.needAck = f.varint != 0;
			break;
		case 10:
			r.pushServer = f.bytes;
			break;
		default:
			break;
		}
	}
	return r;
}

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
		if (msg.first != "WebcastChatMessage")
			continue;

		QString nickname, uniqueId, text;
		PbReader chat(msg.second);
		PbField c;
		while (chat.next(c)) {
			if (c.number == 2) {
				PbReader user(c.bytes);
				PbField u;
				while (user.next(u)) {
					if (u.number == 3)
						nickname = QString::fromUtf8(u.bytes);
					else if (u.number == 38)
						uniqueId = QString::fromUtf8(u.bytes);
				}
			} else if (c.number == 3) {
				text = QString::fromUtf8(c.bytes);
			}
		}
		emitMessage(nickname.isEmpty() ? uniqueId : nickname, QString(), text);
	}
}
