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

struct IrcMessage;

/* Anonymous (read-only) Twitch IRC over WebSocket. */
class TwitchChat : public ChatConnector {
	Q_OBJECT

public:
	TwitchChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeChannel(const QString &input);

	/* One IRC line from the server; public so tests can feed it. */
	void handleLine(const QByteArray &line);

protected:
	void connectNow() override;
	void disconnectNow() override;

private:
	void handleUserNotice(const IrcMessage &irc);

	WsClient m_ws;
	QString m_channel;
};
