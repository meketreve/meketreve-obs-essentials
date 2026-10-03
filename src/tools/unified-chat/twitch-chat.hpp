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

#include <functional>

struct IrcMessage;

/* Anonymous (read-only) Twitch IRC over WebSocket. */
class TwitchChat : public ChatConnector {
	Q_OBJECT

public:
	TwitchChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeChannel(const QString &input);
	/* The IRC "emotes" tag ("25:0-4,6-10/1902:12-16", positions in code
	 * points) as pictures over the UTF-16 text. */
	static QList<ChatEmote> parseEmotes(const QString &tag, const QString &text);

	/* One IRC line from the server; public so tests can feed it. */
	void handleLine(const QByteArray &line);

	/* Anonymous IRC has no viewer count: the logged-in account asks Helix. */
	using ViewerLookup = std::function<void(const QString &channel, std::function<void(int viewers)> done)>;
	void setViewerLookup(ViewerLookup lookup) { m_viewerLookup = std::move(lookup); }

protected:
	void connectNow() override;
	void disconnectNow() override;
	void fetchViewers() override;

private:
	void handleUserNotice(const IrcMessage &irc);

	WsClient m_ws;
	QString m_channel;
	ViewerLookup m_viewerLookup;
};
