/*
Meketreve OBS Essentials - Texuguito bot
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
#include "overlay-server.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace {

constexpr qsizetype kMaxRequest = 16 * 1024;
constexpr qsizetype kMaxFrame = 64 * 1024;

QByteArray mimeFor(const QString &path)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	if (ext == QLatin1String("html"))
		return "text/html; charset=utf-8";
	if (ext == QLatin1String("js"))
		return "text/javascript; charset=utf-8";
	if (ext == QLatin1String("json"))
		return "application/json";
	if (ext == QLatin1String("css"))
		return "text/css";
	if (ext == QLatin1String("png"))
		return "image/png";
	if (ext == QLatin1String("mp3"))
		return "audio/mpeg";
	if (ext == QLatin1String("wav"))
		return "audio/wav";
	if (ext == QLatin1String("ogg"))
		return "audio/ogg";
	return "application/octet-stream";
}

QByteArray statusText(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 400:
		return "Bad Request";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	default:
		return "Error";
	}
}

} // namespace

OverlayServer::OverlayServer(Routes routes, QObject *parent) : QObject(parent), m_routes(std::move(routes))
{
	m_server = new QTcpServer(this);
	connect(m_server, &QTcpServer::newConnection, this, &OverlayServer::onNewConnection);
}

OverlayServer::~OverlayServer()
{
	close();
}

bool OverlayServer::listen(quint16 port)
{
	close();
	return m_server->listen(QHostAddress::LocalHost, port);
}

void OverlayServer::close()
{
	m_server->close();
	const auto sockets = m_clients.keys();
	for (QTcpSocket *s : sockets)
		dropClient(s);
}

quint16 OverlayServer::port() const
{
	return m_server->serverPort();
}

bool OverlayServer::isListening() const
{
	return m_server->isListening();
}

int OverlayServer::clientCount() const
{
	int n = 0;
	for (const Client &c : m_clients) {
		if (c.websocket)
			n++;
	}
	return n;
}

QByteArray OverlayServer::acceptKey(const QByteArray &clientKey)
{
	return QCryptographicHash::hash(clientKey.trimmed() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
					QCryptographicHash::Sha1)
		.toBase64();
}

QString OverlayServer::resolvePath(const QString &baseDir, const QString &urlPath)
{
	/* Refuse anything that would climb out of the served folder. */
	const QString decoded = QUrl::fromPercentEncoding(urlPath.toUtf8());
	if (baseDir.isEmpty() || decoded.contains(QStringLiteral("..")))
		return QString();
	const QString base = QDir(baseDir).canonicalPath();
	const QString full = QFileInfo(QDir(base).filePath(decoded)).canonicalFilePath();
	if (base.isEmpty() || full.isEmpty() || !full.startsWith(base + QLatin1Char('/')))
		return QString();
	return full;
}

void OverlayServer::onNewConnection()
{
	while (QTcpSocket *socket = m_server->nextPendingConnection()) {
		m_clients.insert(socket, Client());
		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { dropClient(socket); });
	}
}

void OverlayServer::dropClient(QTcpSocket *socket)
{
	const auto it = m_clients.find(socket);
	if (it == m_clients.end())
		return;
	const bool wasWebSocket = it->websocket;
	m_clients.erase(it);
	socket->disconnect(this);
	socket->abort();
	socket->deleteLater();
	if (wasWebSocket)
		emit clientsChanged(clientCount());
}

void OverlayServer::onReadyRead(QTcpSocket *socket)
{
	auto it = m_clients.find(socket);
	if (it == m_clients.end())
		return;
	it->buffer += socket->readAll();
	if (it->websocket) {
		readFrames(socket, it.value());
		return;
	}
	const qsizetype end = it->buffer.indexOf("\r\n\r\n");
	if (end < 0) {
		if (it->buffer.size() > kMaxRequest)
			dropClient(socket);
		return;
	}
	const QByteArray request = it->buffer.left(end);
	it->buffer.remove(0, end + 4);
	handleHttp(socket, request);
}

void OverlayServer::sendResponse(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body)
{
	/* OBS's browser caches aggressively; always serve the current files. */
	socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) +
		      "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
		      "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
	socket->write(body);
	socket->disconnectFromHost();
}

void OverlayServer::sendFile(QTcpSocket *socket, const QString &path)
{
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		sendResponse(socket, 404, "text/plain", "not found");
		return;
	}
	sendResponse(socket, 200, mimeFor(path), file.readAll());
}

void OverlayServer::handleHttp(QTcpSocket *socket, const QByteArray &request)
{
	const QList<QByteArray> lines = request.split('\n');
	const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
	if (first.size() < 2) {
		sendResponse(socket, 400, "text/plain", "bad request");
		return;
	}
	if (first[0] != "GET") {
		sendResponse(socket, 405, "text/plain", "method not allowed");
		return;
	}
	const QString path = QUrl(QString::fromLatin1(first[1])).path(QUrl::FullyEncoded);

	QHash<QByteArray, QByteArray> headers;
	for (qsizetype i = 1; i < lines.size(); i++) {
		const qsizetype colon = lines[i].indexOf(':');
		if (colon > 0)
			headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
	}

	if (path == QLatin1String("/ws")) {
		const QByteArray key = headers.value("sec-websocket-key");
		if (key.isEmpty() || !headers.value("upgrade").toLower().contains("websocket")) {
			sendResponse(socket, 400, "text/plain", "websocket expected");
			return;
		}
		socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			      "Sec-WebSocket-Accept: " +
			      acceptKey(key) + "\r\n\r\n");
		Client &client = m_clients[socket];
		client.websocket = true;
		if (m_routes.snapshot)
			sendFrame(socket, 0x1, QJsonDocument(m_routes.snapshot()).toJson(QJsonDocument::Compact));
		emit clientsChanged(clientCount());
		if (!client.buffer.isEmpty())
			readFrames(socket, client);
		return;
	}
	if (path == QLatin1String("/overlay")) {
		sendFile(socket, QDir(m_routes.webDir).filePath(QStringLiteral("overlay.html")));
		return;
	}
	if (path.startsWith(QLatin1String("/static/"))) {
		sendFile(socket, resolvePath(m_routes.webDir, path.mid(8)));
		return;
	}
	if (path.startsWith(QLatin1String("/audios/")) && m_routes.audioDir) {
		sendFile(socket, resolvePath(m_routes.audioDir(), path.mid(8)));
		return;
	}
	if (path.startsWith(QLatin1String("/tts/")) && m_routes.ttsClip) {
		const QByteArray mp3 = m_routes.ttsClip(path.mid(5));
		if (mp3.isEmpty())
			sendResponse(socket, 404, "text/plain", "not found");
		else
			sendResponse(socket, 200, "audio/mpeg", mp3);
		return;
	}
	sendResponse(socket, 404, "text/plain", "not found");
}

void OverlayServer::readFrames(QTcpSocket *socket, Client &client)
{
	QByteArray &buf = client.buffer;
	while (buf.size() >= 2) {
		const auto b0 = static_cast<quint8>(buf[0]);
		const auto b1 = static_cast<quint8>(buf[1]);
		const quint8 opcode = b0 & 0x0F;
		const bool masked = (b1 & 0x80) != 0;
		quint64 len = b1 & 0x7F;
		qsizetype pos = 2;
		if (len == 126) {
			if (buf.size() < 4)
				return;
			len = (static_cast<quint64>(static_cast<quint8>(buf[2])) << 8) | static_cast<quint8>(buf[3]);
			pos = 4;
		} else if (len == 127) {
			/* The overlay never sends anything that big. */
			dropClient(socket);
			return;
		}
		if (len > static_cast<quint64>(kMaxFrame)) {
			dropClient(socket);
			return;
		}
		const qsizetype need = pos + (masked ? 4 : 0) + static_cast<qsizetype>(len);
		if (buf.size() < need)
			return;
		QByteArray payload = buf.mid(pos + (masked ? 4 : 0), static_cast<qsizetype>(len));
		if (masked) {
			const QByteArray mask = buf.mid(pos, 4);
			for (qsizetype i = 0; i < payload.size(); i++)
				payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
		}
		buf.remove(0, need);

		if (opcode == 0x8) {
			sendFrame(socket, 0x8, payload.left(2));
			dropClient(socket);
			return;
		}
		if (opcode == 0x9)
			sendFrame(socket, 0xA, payload);
		/* Text from the overlay is ignored, as in the Python version. */
	}
}

void OverlayServer::sendFrame(QTcpSocket *socket, quint8 opcode, const QByteArray &payload)
{
	QByteArray frame;
	frame.append(static_cast<char>(0x80 | opcode));
	const auto len = static_cast<quint64>(payload.size());
	if (len < 126) {
		frame.append(static_cast<char>(len));
	} else if (len < 65536) {
		frame.append(static_cast<char>(126));
		frame.append(static_cast<char>((len >> 8) & 0xFF));
		frame.append(static_cast<char>(len & 0xFF));
	} else {
		frame.append(static_cast<char>(127));
		for (int shift = 56; shift >= 0; shift -= 8)
			frame.append(static_cast<char>((len >> shift) & 0xFF));
	}
	frame += payload;
	socket->write(frame);
}

void OverlayServer::broadcast(const QJsonObject &message)
{
	const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
	for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
		if (it->websocket)
			sendFrame(it.key(), 0x1, json);
	}
}
