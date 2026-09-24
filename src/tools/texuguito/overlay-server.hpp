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
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

/* Small HTTP + WebSocket server for the overlay page (what FastAPI did in the
 * Python bot): /overlay, /static/..., /audios/..., /tts/<id> and /ws. Only
 * listens on this computer. */
class OverlayServer : public QObject {
	Q_OBJECT

public:
	struct Routes {
		QString webDir;
		std::function<QString()> audioDir;
		std::function<QByteArray(const QString &id)> ttsClip;
		std::function<QJsonObject()> snapshot;
	};

	explicit OverlayServer(Routes routes, QObject *parent = nullptr);
	~OverlayServer() override;

	bool listen(quint16 port);
	void close();
	quint16 port() const;
	bool isListening() const;
	int clientCount() const;
	void broadcast(const QJsonObject &message);

	/* Exposed for tests. */
	static QByteArray acceptKey(const QByteArray &clientKey);
	static QString resolvePath(const QString &baseDir, const QString &urlPath);

signals:
	void clientsChanged(int count);

private:
	struct Client {
		QByteArray buffer;
		bool websocket = false;
	};

	void onNewConnection();
	void onReadyRead(QTcpSocket *socket);
	void handleHttp(QTcpSocket *socket, const QByteArray &request);
	void readFrames(QTcpSocket *socket, Client &client);
	void sendFrame(QTcpSocket *socket, quint8 opcode, const QByteArray &payload);
	void sendFile(QTcpSocket *socket, const QString &path);
	void sendResponse(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body);
	void dropClient(QTcpSocket *socket);

	Routes m_routes;
	QTcpServer *m_server = nullptr;
	QHash<QTcpSocket *, Client> m_clients;
};
