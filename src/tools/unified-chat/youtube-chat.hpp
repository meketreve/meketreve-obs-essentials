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

#include <QJsonObject>
#include <QPointer>

class QNetworkReply;

/* YouTube live chat through the same InnerTube endpoint the popout chat
 * uses: no API key and no daily quota. A channel handle is re-checked every
 * minute until it goes live. */
class YouTubeChat : public ChatConnector {
	Q_OBJECT

public:
	YouTubeChat(QNetworkAccessManager *net, QObject *parent);

	/* Returns a video id when the input already names one, else empty. */
	static QString videoIdFromInput(const QString &input);
	static QString liveUrlFromInput(const QString &input);

	/* One entry of get_live_chat's "actions"; public so tests can feed it. */
	void handleAction(const QJsonObject &action);

protected:
	void connectNow() override;
	void disconnectNow() override;

private:
	QNetworkReply *get(const QUrl &url);
	void onLivePage(QNetworkReply *reply);
	void loadChatPage(const QString &videoId);
	void onChatPage(QNetworkReply *reply);
	void poll();
	void onPoll(QNetworkReply *reply);

	QPointer<QNetworkReply> m_pending;
	QTimer m_pollTimer;
	QString m_apiKey;
	QString m_clientVersion;
	QString m_continuation;
	bool m_skipBacklog = false;
};
