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

#include <functional>

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

	/* Asked when the channel page shows no public live: the logged-in
	 * account's live video on that channel (unlisted ones too), or empty. */
	using LiveLookup =
		std::function<void(const QString &channelId, std::function<void(const QString &videoId)> done)>;
	void setLiveLookup(LiveLookup lookup) { m_liveLookup = std::move(lookup); }
	/* "https://www.youtube.com/channel/UC…" in the page -> "UC…". */
	static QString channelIdFromPage(const QByteArray &html);
	/* updated_metadata's "watching now" count, -1 when it has none. */
	static int viewersFromMetadata(const QJsonObject &root);

protected:
	void connectNow() override;
	void disconnectNow() override;
	void fetchViewers() override;

private:
	QNetworkReply *get(const QUrl &url);
	void onLivePage(QNetworkReply *reply);
	void loadChatPage(const QString &videoId);
	void onChatPage(QNetworkReply *reply);
	void poll();
	void onPoll(QNetworkReply *reply);

	QPointer<QNetworkReply> m_pending;
	QPointer<QNetworkReply> m_viewersReply;
	QTimer m_pollTimer;
	QString m_videoId;
	QString m_apiKey;
	QString m_clientVersion;
	QString m_continuation;
	bool m_skipBacklog = false;
	LiveLookup m_liveLookup;
	qint64 m_lastLookup = 0; /* ms since epoch; the API has a daily quota */
	quint64 m_lookupSerial = 0;
};
