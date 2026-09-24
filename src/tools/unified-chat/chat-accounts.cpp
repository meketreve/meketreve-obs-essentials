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
#include "chat-accounts.hpp"

#include "kick-chat.hpp"
#include "oauth-util.hpp"
#include "twitch-chat.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include <algorithm>

namespace {

const char *const kTwitchScopes =
	"user:read:chat user:write:chat moderator:manage:banned_users moderator:manage:chat_messages "
	"moderator:read:followers moderator:read:chatters";
const char *const kKickScopes = "user:read channel:read chat:write moderation:ban moderation:chat_message:manage";

size_t slot(ChatPlatform p)
{
	return p == ChatPlatform::Kick ? 1 : 0;
}

const char *platformKey(ChatPlatform p)
{
	return p == ChatPlatform::Kick ? "kick" : "twitch";
}

/* Twitch and Kick both answer errors as {"message": ...} (Twitch device
 * flow also uses it for "authorization_pending"). */
QString errorText(const QJsonObject &body, const QString &fallback)
{
	for (const char *key : {"message", "error_description", "error"}) {
		const QString v = body.value(QLatin1String(key)).toString();
		if (!v.isEmpty())
			return v;
	}
	return fallback;
}

} // namespace

QString ChatAccounts::kickRedirectUri()
{
	return QStringLiteral("http://localhost:%1/callback").arg(kKickRedirectPort);
}

ChatAccounts::ChatAccounts(const QString &storePath, QObject *parent) : QObject(parent), m_storePath(storePath)
{
	load();
	connect(&m_devicePoll, &QTimer::timeout, this, &ChatAccounts::pollTwitchDevice);
	connect(&m_eventSub, &WsClient::textReceived, this, &ChatAccounts::onEventSubText);
}

ChatAccounts::~ChatAccounts()
{
	cancelLogin();
}

ChatAccount &ChatAccounts::acc(ChatPlatform p)
{
	return m_accounts[slot(p)];
}

const ChatAccount &ChatAccounts::account(ChatPlatform p) const
{
	return m_accounts[slot(p)];
}

bool ChatAccounts::canLogIn(ChatPlatform p) const
{
	const ChatAccount &a = account(p);
	if (p == ChatPlatform::Kick)
		return !a.clientId.isEmpty() && !a.clientSecret.isEmpty();
	return p == ChatPlatform::Twitch && !a.clientId.isEmpty();
}

void ChatAccounts::load()
{
	QFile file(m_storePath);
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	for (ChatPlatform p : {ChatPlatform::Twitch, ChatPlatform::Kick}) {
		const QJsonObject o = root.value(QLatin1String(platformKey(p))).toObject();
		ChatAccount &a = acc(p);
		a.clientId = o.value(QStringLiteral("clientId")).toString();
		a.clientSecret = o.value(QStringLiteral("clientSecret")).toString();
		a.accessToken = o.value(QStringLiteral("accessToken")).toString();
		a.refreshToken = o.value(QStringLiteral("refreshToken")).toString();
		a.userId = o.value(QStringLiteral("userId")).toString();
		a.login = o.value(QStringLiteral("login")).toString();
		a.expiresAt = static_cast<qint64>(o.value(QStringLiteral("expiresAt")).toDouble());
	}
}

void ChatAccounts::save()
{
	QJsonObject root;
	for (ChatPlatform p : {ChatPlatform::Twitch, ChatPlatform::Kick}) {
		const ChatAccount &a = account(p);
		root.insert(QLatin1String(platformKey(p)),
			    QJsonObject{{QStringLiteral("clientId"), a.clientId},
					{QStringLiteral("clientSecret"), a.clientSecret},
					{QStringLiteral("accessToken"), a.accessToken},
					{QStringLiteral("refreshToken"), a.refreshToken},
					{QStringLiteral("userId"), a.userId},
					{QStringLiteral("login"), a.login},
					{QStringLiteral("expiresAt"), static_cast<double>(a.expiresAt)}});
	}
	QSaveFile file(m_storePath);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.commit();
		QFile::setPermissions(m_storePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	}
}

void ChatAccounts::setClient(ChatPlatform p, const QString &clientId, const QString &clientSecret)
{
	ChatAccount &a = acc(p);
	if (a.clientId == clientId.trimmed() && a.clientSecret == clientSecret.trimmed())
		return;
	/* Tokens belong to the old app. */
	if (a.clientId != clientId.trimmed())
		a = ChatAccount();
	a.clientId = clientId.trimmed();
	a.clientSecret = clientSecret.trimmed();
	save();
	emit accountChanged(p);
}

void ChatAccounts::logOut(ChatPlatform p)
{
	ChatAccount &a = acc(p);
	if (p == ChatPlatform::Twitch && !a.accessToken.isEmpty()) {
		/* Best effort: revoke so the token cannot be reused. */
		post(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/revoke")),
		     OAuthUtil::formBody(
			     {{QStringLiteral("client_id"), a.clientId}, {QStringLiteral("token"), a.accessToken}}),
		     [](int, const QJsonObject &, const QString &) {});
		m_eventSub.close();
	}
	a.accessToken.clear();
	a.refreshToken.clear();
	a.userId.clear();
	a.login.clear();
	a.expiresAt = 0;
	save();
	emit accountChanged(p);
}

void ChatAccounts::cancelLogin()
{
	m_devicePoll.stop();
	m_deviceCode.clear();
	for (QTcpServer *s : m_callbackServers) {
		s->close();
		s->deleteLater();
	}
	m_callbackServers.clear();
	m_kickVerifier.clear();
	m_kickState.clear();
}

void ChatAccounts::post(const QUrl &url, const QByteArray &form, Done done)
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	req.setTransferTimeout(20000);
	QNetworkReply *reply = m_net.post(req, form);
	connect(reply, &QNetworkReply::finished, this, [reply, done]() {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
		done(status, body,
		     status >= 200 && status < 300
			     ? QString()
			     : errorText(body, QStringLiteral("HTTP %1 %2").arg(status).arg(reply->errorString())));
	});
}

void ChatAccounts::logIn(ChatPlatform p)
{
	cancelLogin();
	if (!canLogIn(p)) {
		emit loginFailed(p, QStringLiteral("missing client id"));
		return;
	}
	const ChatAccount &a = account(p);

	if (p == ChatPlatform::Twitch) {
		post(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/device")),
		     OAuthUtil::formBody({{QStringLiteral("client_id"), a.clientId},
					  {QStringLiteral("scopes"), QString::fromLatin1(kTwitchScopes)}}),
		     [this](int, const QJsonObject &body, const QString &error) {
			     if (!error.isEmpty()) {
				     emit loginFailed(ChatPlatform::Twitch, error);
				     return;
			     }
			     m_deviceCode = body.value(QStringLiteral("device_code")).toString();
			     m_deviceDeadline = QDateTime::currentMSecsSinceEpoch() +
						body.value(QStringLiteral("expires_in")).toInt(1800) * 1000LL;
			     m_devicePoll.start(std::max(1, body.value(QStringLiteral("interval")).toInt(5)) * 1000);
			     emit deviceCode(body.value(QStringLiteral("user_code")).toString(),
					     QUrl(body.value(QStringLiteral("verification_uri")).toString()));
		     });
		return;
	}

	/* Kick: authorization code + PKCE, caught by a one-shot local server. */
	for (const QHostAddress &addr :
	     {QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::LocalHostIPv6)}) {
		auto *server = new QTcpServer(this);
		if (server->listen(addr, kKickRedirectPort)) {
			connect(server, &QTcpServer::newConnection, this, &ChatAccounts::onKickCallback);
			m_callbackServers.append(server);
		} else {
			delete server;
		}
	}
	if (m_callbackServers.isEmpty()) {
		emit loginFailed(p, QStringLiteral("port %1 is in use").arg(kKickRedirectPort));
		return;
	}
	m_kickVerifier = OAuthUtil::newCodeVerifier();
	m_kickState = OAuthUtil::newState();
	QUrl url(QStringLiteral("https://id.kick.com/oauth/authorize"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	q.addQueryItem(QStringLiteral("redirect_uri"), kickRedirectUri());
	q.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kKickScopes));
	q.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_kickState));
	q.addQueryItem(QStringLiteral("code_challenge"),
		       QString::fromLatin1(OAuthUtil::codeChallengeS256(m_kickVerifier)));
	q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
	url.setQuery(q);
	emit openBrowser(url);
}

void ChatAccounts::pollTwitchDevice()
{
	if (m_deviceCode.isEmpty())
		return;
	if (QDateTime::currentMSecsSinceEpoch() > m_deviceDeadline) {
		cancelLogin();
		emit loginFailed(ChatPlatform::Twitch, QStringLiteral("the code expired"));
		return;
	}
	const ChatAccount &a = account(ChatPlatform::Twitch);
	post(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token")),
	     OAuthUtil::formBody(
		     {{QStringLiteral("client_id"), a.clientId},
		      {QStringLiteral("scopes"), QString::fromLatin1(kTwitchScopes)},
		      {QStringLiteral("device_code"), m_deviceCode},
		      {QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")}}),
	     [this](int, const QJsonObject &body, const QString &error) {
		     if (m_deviceCode.isEmpty())
			     return;
		     if (error == QLatin1String("authorization_pending"))
			     return;
		     if (error == QLatin1String("slow_down")) {
			     m_devicePoll.setInterval(m_devicePoll.interval() + 5000);
			     return;
		     }
		     cancelLogin();
		     if (!error.isEmpty()) {
			     emit loginFailed(ChatPlatform::Twitch, error);
			     return;
		     }
		     finishLogin(ChatPlatform::Twitch, body);
	     });
}

void ChatAccounts::onKickCallback()
{
	for (QTcpServer *server : m_callbackServers) {
		while (QTcpSocket *socket = server->nextPendingConnection()) {
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
				if (!socket->canReadLine())
					return;
				QUrlQuery query;
				const QString path = OAuthUtil::parseRequestLine(socket->readLine(), query);
				const bool isCallback = path == QLatin1String("/callback");
				const QString code = query.queryItemValue(QStringLiteral("code"));
				const QString state = query.queryItemValue(QStringLiteral("state"));
				const QString denied = query.queryItemValue(QStringLiteral("error"));

				const QByteArray page =
					"<html><body style=\"font-family:sans-serif\"><h3>Meketreve OBS Essentials</h3>"
					"<p>You can close this tab and go back to OBS. / Pode fechar esta aba e voltar ao OBS.</p>"
					"</body></html>";
				socket->write("HTTP/1.1 " + QByteArray(isCallback ? "200 OK" : "404 Not Found") +
					      "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
					      "Content-Length: " +
					      QByteArray::number(isCallback ? page.size() : 0) + "\r\n\r\n" +
					      (isCallback ? page : QByteArray()));
				socket->disconnectFromHost();
				socket->deleteLater();
				if (!isCallback || m_kickState.isEmpty())
					return;

				const QByteArray verifier = m_kickVerifier;
				const bool stateOk = state.toLatin1() == m_kickState;
				cancelLogin();
				if (!denied.isEmpty() || code.isEmpty() || !stateOk) {
					emit loginFailed(ChatPlatform::Kick,
							 !denied.isEmpty() ? denied
							 : stateOk         ? QStringLiteral("no code in the answer")
									   : QStringLiteral("state mismatch"));
					return;
				}
				const ChatAccount &a = account(ChatPlatform::Kick);
				post(QUrl(QStringLiteral("https://id.kick.com/oauth/token")),
				     OAuthUtil::formBody(
					     {{QStringLiteral("code"), code},
					      {QStringLiteral("client_id"), a.clientId},
					      {QStringLiteral("client_secret"), a.clientSecret},
					      {QStringLiteral("redirect_uri"), kickRedirectUri()},
					      {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
					      {QStringLiteral("code_verifier"), QString::fromLatin1(verifier)}}),
				     [this](int, const QJsonObject &body, const QString &error) {
					     if (!error.isEmpty())
						     emit loginFailed(ChatPlatform::Kick, error);
					     else
						     finishLogin(ChatPlatform::Kick, body);
				     });
			});
		}
	}
}

void ChatAccounts::finishLogin(ChatPlatform p, const QJsonObject &token)
{
	ChatAccount &a = acc(p);
	a.accessToken = token.value(QStringLiteral("access_token")).toString();
	const QString refreshToken = token.value(QStringLiteral("refresh_token")).toString();
	if (!refreshToken.isEmpty())
		a.refreshToken = refreshToken;
	a.expiresAt =
		QDateTime::currentMSecsSinceEpoch() + token.value(QStringLiteral("expires_in")).toInt(3600) * 1000LL;
	if (a.accessToken.isEmpty()) {
		emit loginFailed(p, QStringLiteral("no access token in the answer"));
		return;
	}
	save();
	fetchIdentity(p);
}

void ChatAccounts::fetchIdentity(ChatPlatform p)
{
	const QUrl url(p == ChatPlatform::Twitch ? QStringLiteral("https://api.twitch.tv/helix/users")
						 : QStringLiteral("https://api.kick.com/public/v1/users"));
	api(p, "GET", url, QJsonObject(), [this, p](int, const QJsonObject &body, const QString &error) {
		const QJsonObject user = body.value(QStringLiteral("data")).toArray().at(0).toObject();
		ChatAccount &a = acc(p);
		if (p == ChatPlatform::Twitch) {
			a.userId = user.value(QStringLiteral("id")).toString();
			a.login = user.value(QStringLiteral("login")).toString();
		} else {
			a.userId = QString::number(user.value(QStringLiteral("user_id")).toInteger());
			a.login = user.value(QStringLiteral("name")).toString();
		}
		if (!error.isEmpty() || a.userId.isEmpty() || a.userId == QLatin1String("0")) {
			a = ChatAccount{a.clientId, a.clientSecret, {}, {}, {}, {}, 0};
			save();
			emit loginFailed(p, error.isEmpty() ? QStringLiteral("could not read the account") : error);
			emit accountChanged(p);
			return;
		}
		save();
		emit accountChanged(p);
	});
}

void ChatAccounts::refresh(ChatPlatform p, std::function<void(bool)> done)
{
	const ChatAccount &a = account(p);
	if (a.refreshToken.isEmpty()) {
		done(false);
		return;
	}
	QList<QPair<QString, QString>> form{{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
					    {QStringLiteral("refresh_token"), a.refreshToken},
					    {QStringLiteral("client_id"), a.clientId}};
	if (p == ChatPlatform::Kick)
		form.append({QStringLiteral("client_secret"), a.clientSecret});
	post(QUrl(p == ChatPlatform::Twitch ? QStringLiteral("https://id.twitch.tv/oauth2/token")
					    : QStringLiteral("https://id.kick.com/oauth/token")),
	     OAuthUtil::formBody(form), [this, p, done](int, const QJsonObject &body, const QString &error) {
		     if (!error.isEmpty() || body.value(QStringLiteral("access_token")).toString().isEmpty()) {
			     /* The refresh token is dead too: the user must log in again. */
			     ChatAccount &a = acc(p);
			     a.accessToken.clear();
			     a.refreshToken.clear();
			     save();
			     emit accountChanged(p);
			     emit actionFailed(p, QStringLiteral("session expired, log in again"));
			     done(false);
			     return;
		     }
		     ChatAccount &a = acc(p);
		     a.accessToken = body.value(QStringLiteral("access_token")).toString();
		     const QString rt = body.value(QStringLiteral("refresh_token")).toString();
		     if (!rt.isEmpty())
			     a.refreshToken = rt;
		     a.expiresAt = QDateTime::currentMSecsSinceEpoch() +
				   body.value(QStringLiteral("expires_in")).toInt(3600) * 1000LL;
		     save();
		     done(true);
	     });
}

void ChatAccounts::api(ChatPlatform p, const QByteArray &verb, const QUrl &url, const QJsonObject &body, Done done,
		       bool retried)
{
	const ChatAccount &a = account(p);
	if (!a.loggedIn()) {
		done(0, QJsonObject(), QStringLiteral("not logged in"));
		return;
	}
	/* Refresh a minute early rather than eat a 401. */
	if (!retried && a.expiresAt > 0 && QDateTime::currentMSecsSinceEpoch() > a.expiresAt - 60000) {
		refresh(p, [this, p, verb, url, body, done](bool ok) {
			if (ok)
				api(p, verb, url, body, done, true);
			else
				done(401, QJsonObject(), QStringLiteral("session expired"));
		});
		return;
	}

	QNetworkRequest req(url);
	req.setRawHeader("Authorization", "Bearer " + a.accessToken.toUtf8());
	if (p == ChatPlatform::Twitch)
		req.setRawHeader("Client-Id", a.clientId.toUtf8());
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	req.setRawHeader("Accept", "application/json");
	req.setTransferTimeout(20000);
	const QByteArray payload = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
	QNetworkReply *reply = m_net.sendCustomRequest(req, verb, payload);
	connect(reply, &QNetworkReply::finished, this, [this, reply, p, verb, url, body, done, retried]() {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
		if (status == 401 && !retried) {
			refresh(p, [this, p, verb, url, body, done](bool ok) {
				if (ok)
					api(p, verb, url, body, done, true);
				else
					done(401, QJsonObject(), QStringLiteral("session expired"));
			});
			return;
		}
		done(status, answer,
		     status >= 200 && status < 300
			     ? QString()
			     : errorText(answer, QStringLiteral("HTTP %1 %2").arg(status).arg(reply->errorString())));
	});
}

void ChatAccounts::withBroadcaster(ChatPlatform p, const QString &channel, std::function<void(const QString &)> then)
{
	const QString name = p == ChatPlatform::Twitch ? TwitchChat::normalizeChannel(channel)
						       : KickChat::normalizeChannel(channel);
	const QString key = QLatin1String(p == ChatPlatform::Twitch ? "t:" : "k:") + name;
	if (m_broadcasterIds.contains(key)) {
		then(m_broadcasterIds.value(key));
		return;
	}
	QUrl url(p == ChatPlatform::Twitch ? QStringLiteral("https://api.twitch.tv/helix/users")
					   : QStringLiteral("https://api.kick.com/public/v1/channels"));
	url.setQuery(QUrlQuery{{p == ChatPlatform::Twitch ? QStringLiteral("login") : QStringLiteral("slug"), name}});
	api(p, "GET", url, QJsonObject(),
	    [this, p, key, name, then](int, const QJsonObject &body, const QString &error) {
		    const QJsonObject first = body.value(QStringLiteral("data")).toArray().at(0).toObject();
		    const QString id =
			    p == ChatPlatform::Twitch
				    ? first.value(QStringLiteral("id")).toString()
				    : QString::number(first.value(QStringLiteral("broadcaster_user_id")).toInteger());
		    if (id.isEmpty() || id == QLatin1String("0")) {
			    emit actionFailed(p, error.isEmpty() ? QStringLiteral("channel '%1' not found").arg(name)
								 : error);
			    return;
		    }
		    m_broadcasterIds.insert(key, id);
		    then(id);
	    });
}

void ChatAccounts::sendMessage(ChatPlatform p, const QString &channel, const QString &text)
{
	withBroadcaster(p, channel, [this, p, text](const QString &broadcaster) {
		const ChatAccount &a = account(p);
		const auto report = [this, p](int, const QJsonObject &body, const QString &error) {
			if (!error.isEmpty()) {
				emit actionFailed(p, error);
				return;
			}
			/* Twitch answers 200 with is_sent=false when AutoMod or a
			 * chat setting drops the message. */
			const QJsonObject d = body.value(QStringLiteral("data")).toArray().at(0).toObject();
			if (p == ChatPlatform::Twitch && d.contains(QStringLiteral("is_sent")) &&
			    !d.value(QStringLiteral("is_sent")).toBool())
				emit actionFailed(p, d.value(QStringLiteral("drop_reason"))
							     .toObject()
							     .value(QStringLiteral("message"))
							     .toString(QStringLiteral("message dropped")));
		};
		if (p == ChatPlatform::Twitch)
			api(p, "POST", QUrl(QStringLiteral("https://api.twitch.tv/helix/chat/messages")),
			    QJsonObject{{QStringLiteral("broadcaster_id"), broadcaster},
					{QStringLiteral("sender_id"), a.userId},
					{QStringLiteral("message"), text}},
			    report);
		else
			api(p, "POST", QUrl(QStringLiteral("https://api.kick.com/public/v1/chat")),
			    QJsonObject{{QStringLiteral("content"), text.left(500)},
					{QStringLiteral("type"), QStringLiteral("user")},
					{QStringLiteral("broadcaster_user_id"), broadcaster.toLongLong()}},
			    report);
	});
}

void ChatAccounts::timeoutUser(ChatPlatform p, const QString &channel, const QString &userId, int seconds)
{
	withBroadcaster(p, channel, [this, p, userId, seconds](const QString &broadcaster) {
		const auto report = [this, p](int, const QJsonObject &, const QString &error) {
			if (!error.isEmpty())
				emit actionFailed(p, error);
		};
		if (p == ChatPlatform::Twitch) {
			QUrl url(QStringLiteral("https://api.twitch.tv/helix/moderation/bans"));
			url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
					       {QStringLiteral("moderator_id"), account(p).userId}});
			QJsonObject data{{QStringLiteral("user_id"), userId}};
			if (seconds > 0)
				data.insert(QStringLiteral("duration"), seconds);
			api(p, "POST", url, QJsonObject{{QStringLiteral("data"), data}}, report);
		} else {
			QJsonObject body{{QStringLiteral("broadcaster_user_id"), broadcaster.toLongLong()},
					 {QStringLiteral("user_id"), userId.toLongLong()}};
			/* Kick counts timeouts in minutes (1-10080). */
			if (seconds > 0)
				body.insert(QStringLiteral("duration"), std::clamp((seconds + 59) / 60, 1, 10080));
			api(p, "POST", QUrl(QStringLiteral("https://api.kick.com/public/v1/moderation/bans")), body,
			    report);
		}
	});
}

void ChatAccounts::banUser(ChatPlatform p, const QString &channel, const QString &userId)
{
	timeoutUser(p, channel, userId, 0);
}

void ChatAccounts::deleteMessage(ChatPlatform p, const QString &channel, const QString &messageId)
{
	const auto report = [this, p](int, const QJsonObject &, const QString &error) {
		if (!error.isEmpty())
			emit actionFailed(p, error);
	};
	if (p == ChatPlatform::Kick) {
		api(p, "DELETE", QUrl(QStringLiteral("https://api.kick.com/public/v1/chat/%1").arg(messageId)),
		    QJsonObject(), report);
		return;
	}
	withBroadcaster(p, channel, [this, p, messageId, report](const QString &broadcaster) {
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/moderation/chat"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
				       {QStringLiteral("moderator_id"), account(p).userId},
				       {QStringLiteral("message_id"), messageId}});
		api(p, "DELETE", url, QJsonObject(), report);
	});
}

void ChatAccounts::twitchChatters(const QString &channel,
				  std::function<void(const QSet<QString> &, int, const QString &)> done)
{
	if (!account(ChatPlatform::Twitch).loggedIn()) {
		done({}, 0, QStringLiteral("not logged in"));
		return;
	}
	withBroadcaster(ChatPlatform::Twitch, channel, [this, done](const QString &broadcaster) {
		/* One page (up to 1000) is plenty for a parade on screen. */
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/chatters"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
				       {QStringLiteral("moderator_id"), account(ChatPlatform::Twitch).userId},
				       {QStringLiteral("first"), QStringLiteral("1000")}});
		api(ChatPlatform::Twitch, "GET", url, QJsonObject(),
		    [done](int status, const QJsonObject &body, const QString &error) {
			    QSet<QString> logins;
			    for (const QJsonValue v : body.value(QStringLiteral("data")).toArray())
				    logins.insert(
					    v.toObject().value(QStringLiteral("user_login")).toString().toLower());
			    done(logins, status, error);
		    });
	});
}

void ChatAccounts::watchTwitchFollows(const QString &channel)
{
	m_eventSub.close();
	m_followChannelId.clear();
	if (!account(ChatPlatform::Twitch).loggedIn() || channel.trimmed().isEmpty())
		return;
	withBroadcaster(ChatPlatform::Twitch, channel, [this](const QString &broadcaster) {
		m_followChannelId = broadcaster;
		m_eventSub.open(QUrl(QStringLiteral("wss://eventsub.wss.twitch.tv/ws")));
	});
}

void ChatAccounts::onEventSubText(const QByteArray &data)
{
	const QJsonObject msg = QJsonDocument::fromJson(data).object();
	const QString type =
		msg.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("message_type")).toString();
	const QJsonObject payload = msg.value(QStringLiteral("payload")).toObject();

	if (type == QLatin1String("session_welcome")) {
		const QString session =
			payload.value(QStringLiteral("session")).toObject().value(QStringLiteral("id")).toString();
		const QJsonObject sub{
			{QStringLiteral("type"), QStringLiteral("channel.follow")},
			{QStringLiteral("version"), QStringLiteral("2")},
			{QStringLiteral("condition"),
			 QJsonObject{{QStringLiteral("broadcaster_user_id"), m_followChannelId},
				     {QStringLiteral("moderator_user_id"), account(ChatPlatform::Twitch).userId}}},
			{QStringLiteral("transport"),
			 QJsonObject{{QStringLiteral("method"), QStringLiteral("websocket")},
				     {QStringLiteral("session_id"), session}}}};
		api(ChatPlatform::Twitch, "POST",
		    QUrl(QStringLiteral("https://api.twitch.tv/helix/eventsub/subscriptions")), sub,
		    [this](int status, const QJsonObject &, const QString &error) {
			    /* 403: the account is not a moderator of that channel. */
			    if (!error.isEmpty())
				    emit actionFailed(ChatPlatform::Twitch,
						      QStringLiteral("follows (%1): %2").arg(status).arg(error));
		    });
	} else if (type == QLatin1String("session_reconnect")) {
		m_eventSub.open(QUrl(payload.value(QStringLiteral("session"))
					     .toObject()
					     .value(QStringLiteral("reconnect_url"))
					     .toString()));
	} else if (type == QLatin1String("notification")) {
		const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
		ChatMessage follow{ChatPlatform::Twitch, event.value(QStringLiteral("user_name")).toString(), QString(),
				   QString(), QString()};
		follow.event = ChatEvent::Follow;
		follow.userId = event.value(QStringLiteral("user_id")).toString();
		emit eventReceived(follow);
	}
}
