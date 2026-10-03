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

#include <QJsonObject>
#include <QPointer>

class QNetworkReply;

/* Kick chat through its public Pusher channel. */
class KickChat : public ChatConnector {
	Q_OBJECT

public:
	KickChat(QNetworkAccessManager *net, QObject *parent);

	static QString normalizeChannel(const QString &input);
	/* "hi [emote:37226:KEKW]" -> "hi KEKW", with KEKW as a picture. */
	static QString parseEmotes(const QString &content, QList<ChatEmote> &emotes);

	/* One Pusher frame; public so tests can feed it. */
	void handleEvent(const QByteArray &data);
	/* kick.com/api/v2/channels/<slug> -> livestream.viewer_count, -1 when
	 * the channel is not live. */
	static int viewersFromChannel(const QJsonObject &channel);

protected:
	void connectNow() override;
	void disconnectNow() override;
	void fetchViewers() override;

private:
	void onChannelInfo(QNetworkReply *reply);
	void subscribe(const QString &channel);

	WsClient m_ws;
	QPointer<QNetworkReply> m_pending;
	QPointer<QNetworkReply> m_viewersReply;
	QString m_slug; /* empty when the chatroom id was typed instead */
	QString m_chatroomId;
	QString m_channelId;
};
