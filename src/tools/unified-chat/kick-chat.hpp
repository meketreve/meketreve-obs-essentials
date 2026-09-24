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

/* Kick chat through its public Pusher channel. */
class KickChat : public ChatConnector {
	Q_OBJECT

public:
	KickChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeChannel(const QString &input);

protected:
	void connectNow() override;
	void disconnectNow() override;

private:
	void onChannelInfo(QNetworkReply *reply);
	void handleEvent(const QByteArray &data);

	WsClient m_ws;
	QPointer<QNetworkReply> m_pending;
	QString m_chatroomId;
};
