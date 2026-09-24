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

#include "ws-client.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace {

constexpr quint8 kOpContinuation = 0x0;
constexpr quint8 kOpText = 0x1;
constexpr quint8 kOpBinary = 0x2;
constexpr quint8 kOpClose = 0x8;
constexpr quint8 kOpPing = 0x9;
constexpr quint8 kOpPong = 0xA;
constexpr qint64 kMaxMessage = 16 * 1024 * 1024;

QByteArray randomBytes(int count)
{
	QByteArray out(count, Qt::Uninitialized);
	for (int i = 0; i < count; i++)
		out[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
	return out;
}

} // namespace

WsClient::WsClient(QObject *parent) : QObject(parent)
{
	connect(&m_socket, &QSslSocket::encrypted, this, &WsClient::onEncrypted);
	connect(&m_socket, &QSslSocket::connected, this, [this]() {
		if (m_url.scheme() == QLatin1String("ws"))
			onEncrypted();
	});
	connect(&m_socket, &QSslSocket::readyRead, this, &WsClient::onReadyRead);
	connect(&m_socket, &QSslSocket::errorOccurred, this, &WsClient::onSocketError);
	connect(&m_socket, &QSslSocket::disconnected, this, &WsClient::onDisconnected);
}

void WsClient::open(const QUrl &url, const Headers &headers)
{
	close();
	m_url = url;
	m_headers = headers;
	m_buffer.clear();
	m_fragments.clear();
	m_state = State::Connecting;

	const bool secure = url.scheme() == QLatin1String("wss");
	const quint16 port = static_cast<quint16>(url.port(secure ? 443 : 80));
	if (secure)
		m_socket.connectToHostEncrypted(url.host(), port);
	else
		m_socket.connectToHost(url.host(), port);
}

void WsClient::close()
{
	if (m_state == State::Closed)
		return;
	if (m_state == State::Open)
		sendFrame(kOpClose, QByteArray("\x03\xe8", 2));
	m_state = State::Closed;
	m_socket.abort();
}

void WsClient::sendText(const QByteArray &utf8)
{
	if (m_state == State::Open)
		sendFrame(kOpText, utf8);
}

void WsClient::sendBinary(const QByteArray &data)
{
	if (m_state == State::Open)
		sendFrame(kOpBinary, data);
}

void WsClient::onEncrypted()
{
	if (m_state != State::Connecting)
		return;

	m_key = randomBytes(16).toBase64();
	QByteArray path = m_url.path(QUrl::FullyEncoded).toUtf8();
	if (path.isEmpty())
		path = "/";
	if (m_url.hasQuery())
		path += "?" + m_url.query(QUrl::FullyEncoded).toUtf8();

	QByteArray req;
	req += "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + m_url.host().toUtf8() + "\r\n";
	req += "Upgrade: websocket\r\n";
	req += "Connection: Upgrade\r\n";
	req += "Sec-WebSocket-Key: " + m_key + "\r\n";
	req += "Sec-WebSocket-Version: 13\r\n";
	for (const auto &h : m_headers)
		req += h.first + ": " + h.second + "\r\n";
	req += "\r\n";

	m_state = State::Handshake;
	m_socket.write(req);
}

void WsClient::onReadyRead()
{
	m_buffer += m_socket.readAll();

	if (m_state == State::Handshake && !readHandshake())
		return;
	if (m_state == State::Open)
		readFrames();
}

bool WsClient::readHandshake()
{
	const qsizetype end = m_buffer.indexOf("\r\n\r\n");
	if (end < 0) {
		if (m_buffer.size() > 64 * 1024)
			fail(QStringLiteral("handshake too large"));
		return false;
	}

	const QByteArray head = m_buffer.left(end);
	m_buffer.remove(0, end + 4);

	const QList<QByteArray> lines = head.split('\n');
	const QByteArray status = lines.value(0).trimmed();
	if (!status.startsWith("HTTP/1.1 101")) {
		fail(QStringLiteral("HTTP %1").arg(QString::fromUtf8(status.mid(9))));
		return false;
	}

	const QByteArray expected =
		QCryptographicHash::hash(m_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1)
			.toBase64();
	bool accepted = false;
	for (const QByteArray &line : lines) {
		const qsizetype colon = line.indexOf(':');
		if (colon < 0)
			continue;
		if (line.left(colon).trimmed().toLower() == "sec-websocket-accept")
			accepted = line.mid(colon + 1).trimmed() == expected;
	}
	if (!accepted) {
		fail(QStringLiteral("bad Sec-WebSocket-Accept"));
		return false;
	}

	m_state = State::Open;
	emit opened();
	return true;
}

bool WsClient::readFrames()
{
	while (m_state == State::Open) {
		if (m_buffer.size() < 2)
			return true;

		const auto b0 = static_cast<quint8>(m_buffer[0]);
		const auto b1 = static_cast<quint8>(m_buffer[1]);
		const bool fin = (b0 & 0x80) != 0;
		const quint8 opcode = b0 & 0x0F;
		const bool masked = (b1 & 0x80) != 0;
		quint64 len = b1 & 0x7F;
		qsizetype pos = 2;

		if (len == 126) {
			if (m_buffer.size() < 4)
				return true;
			len = (static_cast<quint64>(static_cast<quint8>(m_buffer[2])) << 8) |
			      static_cast<quint8>(m_buffer[3]);
			pos = 4;
		} else if (len == 127) {
			if (m_buffer.size() < 10)
				return true;
			len = 0;
			for (int i = 0; i < 8; i++)
				len = (len << 8) | static_cast<quint8>(m_buffer[2 + i]);
			pos = 10;
		}

		if (len > static_cast<quint64>(kMaxMessage)) {
			fail(QStringLiteral("frame too large"));
			return false;
		}

		QByteArray mask;
		if (masked) {
			if (m_buffer.size() < pos + 4)
				return true;
			mask = m_buffer.mid(pos, 4);
			pos += 4;
		}

		const auto payloadLen = static_cast<qsizetype>(len);
		if (m_buffer.size() < pos + payloadLen)
			return true;

		QByteArray payload = m_buffer.mid(pos, payloadLen);
		m_buffer.remove(0, pos + payloadLen);
		if (masked) {
			for (qsizetype i = 0; i < payload.size(); i++)
				payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
		}

		switch (opcode) {
		case kOpPing:
			sendFrame(kOpPong, payload);
			break;
		case kOpPong:
			break;
		case kOpClose:
			fail(QStringLiteral("closed by server"));
			return false;
		case kOpText:
		case kOpBinary:
		case kOpContinuation: {
			if (opcode != kOpContinuation) {
				m_fragmentOpcode = opcode;
				m_fragments.clear();
			}
			m_fragments += payload;
			if (m_fragments.size() > kMaxMessage) {
				fail(QStringLiteral("message too large"));
				return false;
			}
			if (!fin)
				break;
			const QByteArray message = m_fragments;
			m_fragments.clear();
			if (m_fragmentOpcode == kOpText)
				emit textReceived(message);
			else
				emit binaryReceived(message);
			break;
		}
		default:
			fail(QStringLiteral("unknown opcode %1").arg(opcode));
			return false;
		}
	}
	return true;
}

void WsClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
	QByteArray frame;
	frame.append(static_cast<char>(0x80 | opcode));

	const auto len = static_cast<quint64>(payload.size());
	if (len < 126) {
		frame.append(static_cast<char>(0x80 | len));
	} else if (len <= 0xFFFF) {
		frame.append(static_cast<char>(0x80 | 126));
		frame.append(static_cast<char>((len >> 8) & 0xFF));
		frame.append(static_cast<char>(len & 0xFF));
	} else {
		frame.append(static_cast<char>(0x80 | 127));
		for (int i = 7; i >= 0; i--)
			frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
	}

	const QByteArray mask = randomBytes(4);
	frame += mask;
	QByteArray body = payload;
	for (qsizetype i = 0; i < body.size(); i++)
		body[i] = static_cast<char>(body[i] ^ mask[i % 4]);
	frame += body;

	m_socket.write(frame);
}

void WsClient::onSocketError()
{
	if (m_state != State::Closed)
		fail(m_socket.errorString());
}

void WsClient::onDisconnected()
{
	if (m_state != State::Closed)
		fail(QStringLiteral("connection lost"));
}

void WsClient::fail(const QString &reason)
{
	if (m_state == State::Closed)
		return;
	m_state = State::Closed;
	m_socket.abort();
	emit closed(reason);
}
