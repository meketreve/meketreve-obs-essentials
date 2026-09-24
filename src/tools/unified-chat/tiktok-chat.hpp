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

#pragma once

#include "chat-connector.hpp"
#include "ws-client.hpp"

#include <QPointer>

class QNetworkReply;

/* TikTok LIVE chat. TikTok signs its WebSocket URL with an obfuscated
 * algorithm, so the signed URL comes from the Euler Stream sign server (the
 * same one TikTok-Live-Connector uses); after that the connection is direct. */
class TikTokChat : public ChatConnector {
	Q_OBJECT

public:
	TikTokChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeUser(const QString &input);

protected:
	void connectNow() override;
	void disconnectNow() override;

private:
	QNetworkReply *get(const QUrl &url);
	void onRoomInfo(QNetworkReply *reply);
	void onSignedFetch(QNetworkReply *reply);
	void onFrame(const QByteArray &data);
	void sendHeartbeat();

	WsClient m_ws;
	QPointer<QNetworkReply> m_pending;
	QTimer m_heartbeat;
	QString m_roomId;
	quint64 m_seq = 1;
};
