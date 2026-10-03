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

#pragma once

#include "chat-connector.hpp"
#include "ws-client.hpp"

#include <QJsonObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

/* The plugin's Trovo app: the client id is public (it goes in every API
 * call); the secret stays on the token server. */
extern const char *const kTrovoClientId;

/* Trovo chat through its open chat service: a channel's chat token needs
 * only the app's client id, so reading works without logging in. */
class TrovoChat : public ChatConnector {
	Q_OBJECT

public:
	TrovoChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeChannel(const QString &input);

	/* One chat service frame; public so tests can feed it. */
	void handleFrame(const QByteArray &data);
	/* Messages sent before this (unix seconds) are the history replayed on
	 * connect and are skipped. */
	void setConnectedAt(qint64 seconds) { m_connectedAt = seconds; }

protected:
	void connectNow() override;
	void disconnectNow() override;

private:
	QNetworkReply *api(const QString &path, const QJsonObject *body = nullptr);
	void onUsers(QNetworkReply *reply);
	void requestToken();
	void onToken(QNetworkReply *reply);
	void handleChat(const QJsonObject &chat);
	void ping();

	WsClient m_ws;
	QPointer<QNetworkReply> m_pending;
	QTimer m_pingTimer;
	QString m_channelId;
	QString m_token;
	QString m_authNonce;
	qint64 m_connectedAt = 0;
};
