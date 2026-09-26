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

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <array>
#include <functional>

class QTcpServer;

/* A logged-in account on Twitch or Kick. Twitch defaults to the plugin's own
 * public app (device code, no secret); a user's app can replace it, with an
 * optional secret for a confidential one. Kick needs the user's app and its
 * secret. */
struct ChatAccount {
	QString clientId;
	QString clientSecret;
	QString accessToken;
	QString refreshToken;
	QString userId;
	QString login;
	qint64 expiresAt = 0; /* ms since epoch */

	bool loggedIn() const { return !accessToken.isEmpty(); }
};

/* Someone in a Twitch channel's chat or ban list. */
struct ChatUser {
	QString id;
	QString login;
	QString name;
	/* Ban list only: when a timeout ends (invalid = permanent ban). */
	QDateTime expiresAt;
	QString reason;
	QString moderator;
};

/* Logins (Twitch device code flow for public apps or authorization code with
 * a loopback redirect for confidential ones, Kick OAuth 2.1 + PKCE with a
 * loopback redirect), sending chat, moderation and Twitch follows over EventSub.
 * Tokens are stored as plain text in the module's config folder. */
class ChatAccounts : public QObject {
	Q_OBJECT

public:
	static constexpr quint16 kKickRedirectPort = 53682;
	static QString kickRedirectUri();
	/* Same port as the old texuguito bot, so its Twitch app works as is. */
	static constexpr quint16 kTwitchRedirectPort = 17563;
	static QString twitchRedirectUri();
	/* The plugin's public Twitch app, used while the user sets none. */
	static QString defaultClientId(ChatPlatform p);

	ChatAccounts(const QString &storePath, QObject *parent = nullptr);
	~ChatAccounts() override;

	const ChatAccount &account(ChatPlatform p) const;
	static bool supports(ChatPlatform p) { return p == ChatPlatform::Twitch || p == ChatPlatform::Kick; }
	bool canLogIn(ChatPlatform p) const;
	/* Twitch with a client secret logs in through the browser redirect
	 * instead of the device code. */
	bool usesRedirect(ChatPlatform p) const;
	void setClient(ChatPlatform p, const QString &clientId, const QString &clientSecret);
	void logIn(ChatPlatform p);
	void logOut(ChatPlatform p);
	void cancelLogin();

	/* channel = what the user typed as the chat target (name or link). */
	void sendMessage(ChatPlatform p, const QString &channel, const QString &text);
	/* done (optional) gets the error, empty on success; failures also go
	 * to actionFailed. */
	using ActionDone = std::function<void(const QString &error)>;
	void timeoutUser(ChatPlatform p, const QString &channel, const QString &userId, int seconds,
			 ActionDone done = {});
	void banUser(ChatPlatform p, const QString &channel, const QString &userId, ActionDone done = {});
	/* Lifts a ban or a timeout (both platforms treat them the same). */
	void unbanUser(ChatPlatform p, const QString &channel, const QString &userId, ActionDone done = {});
	void deleteMessage(ChatPlatform p, const QString &channel, const QString &messageId);

	/* Logins in the Twitch channel's chat right now (Helix chatters; needs
	 * the account to be the broadcaster or a moderator there). */
	void twitchChatters(const QString &channel,
			    std::function<void(const QSet<QString> &logins, int status, const QString &error)> done);

	/* Full lists for the viewers window. Chatters need a broadcaster or
	 * moderator login; the ban list only the broadcaster's. Failures before
	 * the request (unknown channel) come through actionFailed. */
	using UsersDone = std::function<void(const QList<ChatUser> &users, const QString &error)>;
	void twitchChatterList(const QString &channel, UsersDone done);
	void twitchBanList(const QString &channel, UsersDone done);

	/* Twitch follows need a moderator token: subscribe over EventSub. */
	void watchTwitchFollows(const QString &channel);

signals:
	void accountChanged(ChatPlatform p);
	void deviceCode(const QString &userCode, const QUrl &verificationUrl);
	void openBrowser(const QUrl &url);
	void loginFailed(ChatPlatform p, const QString &error);
	void actionFailed(ChatPlatform p, const QString &error);
	void eventReceived(const ChatMessage &msg);

private:
	using Done = std::function<void(int status, const QJsonObject &body, const QString &error)>;

	ChatAccount &acc(ChatPlatform p);
	void load();
	void save();
	void finishLogin(ChatPlatform p, const QJsonObject &token);
	void fetchIdentity(ChatPlatform p);
	void pollTwitchDevice();
	bool listenForCallback(quint16 port);
	void onAuthCallback();
	void refresh(ChatPlatform p, std::function<void(bool)> done);
	void api(ChatPlatform p, const QByteArray &verb, const QUrl &url, const QJsonObject &body, Done done,
		 bool retried = false);
	void post(const QUrl &url, const QByteArray &form, Done done);
	void withBroadcaster(ChatPlatform p, const QString &channel, std::function<void(const QString &)> then);
	/* GET every page of a Helix list (cursor pagination), up to maxPages. */
	void helixPages(const QUrl &url, int maxPages,
			std::function<void(const QJsonArray &data, const QString &error)> done,
			QJsonArray collected = {}, const QString &cursor = {});
	void onEventSubText(const QByteArray &data);

	QString m_storePath;
	QNetworkAccessManager m_net;
	std::array<ChatAccount, 2> m_accounts;    /* Twitch, Kick */
	QHash<QString, QString> m_broadcasterIds; /* "t:login" / "k:slug" -> id */

	/* Twitch device flow in progress. */
	QString m_deviceCode;
	QTimer m_devicePoll;
	qint64 m_deviceDeadline = 0;

	/* Browser authorization in progress (Kick, or Twitch with a secret). */
	QList<QTcpServer *> m_callbackServers;
	ChatPlatform m_authPlatform = ChatPlatform::Kick;
	QByteArray m_authVerifier; /* PKCE, Kick only */
	QByteArray m_authState;

	WsClient m_eventSub;
	QString m_followChannelId;
};
