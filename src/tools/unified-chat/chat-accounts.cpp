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
#include <QLocale>
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
	"moderator:read:followers moderator:read:chatters channel:manage:broadcast";
const char *const kTwitchClientId = "r61r2wwuew4j0e8uohmwif5022r4bc";
/* Kick has no public clients: the plugin's app keeps its secret on the
 * project's server (server/kick-oauth), which adds it to token requests. */
const char *const kKickClientId = "01M3G2KSP604PWBG4X8Y3JMNF2";
const char *const kKickTokenProxy = "https://204-216-150-248.sslip.io/kick/token";
/* Google treats a desktop client's secret as public, but the plugin's source
 * is on GitHub, so it lives on the same server as Kick's. */
const char *const kGoogleClientId = "197537815672-gosq83ubonki67m1ndml74od1nq5l5dm.apps.googleusercontent.com";
const char *const kGoogleTokenProxy = "https://204-216-150-248.sslip.io/google/token";
const char *const kGoogleScopes = "https://www.googleapis.com/auth/youtube";
const char *const kYouTubeApi = "https://www.googleapis.com/youtube/v3/";
const char *const kKickScopes =
	"user:read channel:read channel:write chat:write moderation:ban moderation:chat_message:manage";

size_t slot(ChatPlatform p)
{
	switch (p) {
	case ChatPlatform::Twitch:
		return 0;
	case ChatPlatform::Kick:
		return 1;
	case ChatPlatform::YouTube:
		return 2;
	case ChatPlatform::Trovo:
		return 3;
	}
	return 0;
}

const char *platformKey(ChatPlatform p)
{
	switch (p) {
	case ChatPlatform::Twitch:
		return "twitch";
	case ChatPlatform::Kick:
		return "kick";
	case ChatPlatform::YouTube:
		return "youtube";
	case ChatPlatform::Trovo:
		return "trovo";
	}
	return "twitch";
}

QUrl youtubeUrl(const QString &path, const QUrlQuery &query = {})
{
	QUrl url(QString::fromLatin1(kYouTubeApi) + path);
	url.setQuery(query);
	return url;
}

/* Twitch and Kick both answer errors as {"message": ...} (Twitch device
 * flow also uses it for "authorization_pending"); Google API errors are
 * {"error": {"message": ...}}. */
QString errorText(const QJsonObject &body, const QString &fallback)
{
	const QString google =
		body.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
	if (!google.isEmpty())
		return google;
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

QString ChatAccounts::defaultClientId(ChatPlatform p)
{
	return QString::fromLatin1(p == ChatPlatform::Twitch    ? kTwitchClientId
				   : p == ChatPlatform::YouTube ? kGoogleClientId
								: kKickClientId);
}

QString ChatAccounts::youtubeRedirectUri()
{
	return QStringLiteral("http://127.0.0.1:%1/callback").arg(kYouTubeRedirectPort);
}

QString ChatAccounts::redirectUri(ChatPlatform p)
{
	return p == ChatPlatform::Twitch    ? twitchRedirectUri()
	       : p == ChatPlatform::YouTube ? youtubeRedirectUri()
					    : kickRedirectUri();
}

QString ChatAccounts::twitchRedirectUri()
{
	return QStringLiteral("http://localhost:%1").arg(kTwitchRedirectPort);
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
	if (p == ChatPlatform::Kick || p == ChatPlatform::YouTube)
		return !a.clientId.isEmpty() && (!a.clientSecret.isEmpty() || a.clientId == defaultClientId(p));
	return p == ChatPlatform::Twitch && !a.clientId.isEmpty();
}

QUrl ChatAccounts::tokenUrl(ChatPlatform p) const
{
	if (p == ChatPlatform::Twitch)
		return QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token"));
	/* The plugin's own Kick and Google apps go through the server that holds
	 * their secrets. */
	const ChatAccount &a = account(p);
	const bool viaServer = a.clientSecret.isEmpty() && a.clientId == defaultClientId(p);
	if (p == ChatPlatform::YouTube)
		return QUrl(viaServer ? QString::fromLatin1(kGoogleTokenProxy)
				      : QStringLiteral("https://oauth2.googleapis.com/token"));
	if (viaServer)
		return QUrl(QString::fromLatin1(kKickTokenProxy));
	return QUrl(QStringLiteral("https://id.kick.com/oauth/token"));
}

bool ChatAccounts::usesRedirect(ChatPlatform p) const
{
	return p == ChatPlatform::Kick || p == ChatPlatform::YouTube ||
	       (p == ChatPlatform::Twitch && !account(p).clientSecret.isEmpty());
}

void ChatAccounts::load()
{
	for (ChatPlatform p : loginPlatforms())
		acc(p).clientId = defaultClientId(p);
	QFile file(m_storePath);
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	for (ChatPlatform p : loginPlatforms()) {
		const QJsonObject o = root.value(QLatin1String(platformKey(p))).toObject();
		ChatAccount &a = acc(p);
		a.clientId = o.value(QStringLiteral("clientId")).toString();
		if (a.clientId.isEmpty())
			a.clientId = defaultClientId(p);
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
	for (ChatPlatform p : loginPlatforms()) {
		const ChatAccount &a = account(p);
		root.insert(QLatin1String(platformKey(p)),
			    QJsonObject{{QStringLiteral("clientId"),
					 a.clientId == defaultClientId(p) ? QString() : a.clientId},
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
	const QString id = clientId.trimmed().isEmpty() ? defaultClientId(p) : clientId.trimmed();
	if (a.clientId == id && a.clientSecret == clientSecret.trimmed())
		return;
	/* Tokens belong to the old app. */
	if (a.clientId != id)
		a = ChatAccount();
	a.clientId = id;
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
	if (p == ChatPlatform::YouTube && !a.accessToken.isEmpty())
		post(QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")),
		     OAuthUtil::formBody({{QStringLiteral("token"), a.accessToken}}),
		     [](int, const QJsonObject &, const QString &) {});
	m_youtubeBans.clear();
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
	closeCallbackServers();
	m_authVerifier.clear();
	m_authState.clear();
}

void ChatAccounts::closeCallbackServers()
{
	m_callbackGeneration++;
	for (QTcpServer *s : m_callbackServers) {
		s->close();
		s->deleteLater();
	}
	m_callbackServers.clear();
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

	if (!usesRedirect(p)) {
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

	/* Authorization code caught by a one-shot local server: Kick and Google
	 * with PKCE, Twitch (confidential app) with the client secret. */
	const bool kick = p == ChatPlatform::Kick;
	const bool google = p == ChatPlatform::YouTube;
	const quint16 port = kick ? kKickRedirectPort : google ? kYouTubeRedirectPort : kTwitchRedirectPort;
	if (!listenForCallback(port)) {
		emit loginFailed(p, QStringLiteral("port %1 is in use").arg(port));
		return;
	}
	m_authPlatform = p;
	m_authState = OAuthUtil::newState();
	QUrl url(kick     ? QStringLiteral("https://id.kick.com/oauth/authorize")
		 : google ? QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")
			  : QStringLiteral("https://id.twitch.tv/oauth2/authorize"));
	QUrlQuery q;
	q.addQueryItem(QStringLiteral("client_id"), a.clientId);
	q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri(p));
	q.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kick     ? kKickScopes
								    : google ? kGoogleScopes
									     : kTwitchScopes));
	q.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_authState));
	if (google) {
		/* A refresh token comes only with offline access and a fresh consent. */
		q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
		q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
	}
	if (kick || google) {
		m_authVerifier = OAuthUtil::newCodeVerifier();
		q.addQueryItem(QStringLiteral("code_challenge"),
			       QString::fromLatin1(OAuthUtil::codeChallengeS256(m_authVerifier)));
		q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
	} else {
		/* The old bot may still hold a session on this app. */
		q.addQueryItem(QStringLiteral("force_verify"), QStringLiteral("true"));
	}
	url.setQuery(q);
	emit openBrowser(url);
}

bool ChatAccounts::listenForCallback(quint16 port)
{
	for (const QHostAddress &addr :
	     {QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::LocalHostIPv6)}) {
		auto *server = new QTcpServer(this);
		if (server->listen(addr, port)) {
			connect(server, &QTcpServer::newConnection, this, &ChatAccounts::onAuthCallback);
			m_callbackServers.append(server);
		} else {
			delete server;
		}
	}
	return !m_callbackServers.isEmpty();
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

void ChatAccounts::onAuthCallback()
{
	for (QTcpServer *server : m_callbackServers) {
		while (QTcpSocket *socket = server->nextPendingConnection()) {
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
				if (!socket->canReadLine())
					return;
				const ChatPlatform p = m_authPlatform;
				QUrlQuery query;
				const QString path = OAuthUtil::parseRequestLine(socket->readLine(), query);
				const bool isCallback = path ==
							QLatin1String(p == ChatPlatform::Twitch ? "/" : "/callback");
				const QString code = query.queryItemValue(QStringLiteral("code"));
				const QString state = query.queryItemValue(QStringLiteral("state"));
				QString denied =
					query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded);
				if (denied.isEmpty())
					denied = query.queryItemValue(QStringLiteral("error"));
				denied.replace(QLatin1Char('+'), QLatin1Char(' '));

				const QByteArray page =
					"<html><body style=\"font-family:sans-serif\"><h3>Meketreve OBS Essentials</h3>"
					"<p>You can close this tab and go back to OBS. / Pode fechar esta aba e voltar ao OBS.</p>"
					"</body></html>";
				socket->write("HTTP/1.1 " + QByteArray(isCallback ? "200 OK" : "404 Not Found") +
					      "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
					      "Content-Length: " +
					      QByteArray::number(isCallback ? page.size() : 0) + "\r\n\r\n" +
					      (isCallback ? page : QByteArray()));
				/* Delete only once the page is out: deleting right away
				 * dropped it, the browser saw an empty reply and retried. */
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				socket->disconnectFromHost();
				if (!isCallback || m_authState.isEmpty())
					return;

				const QByteArray verifier = m_authVerifier;
				const bool stateOk = state.toLatin1() == m_authState;
				/* Done with the code, but browsers may load the redirect twice
				 * (Kick redirects from script): keep answering "you can close
				 * this tab" for a while instead of refusing the connection. */
				m_authVerifier.clear();
				m_authState.clear();
				QTimer::singleShot(kCallbackLingerMs, this,
						   [this, generation = m_callbackGeneration]() {
							   if (generation == m_callbackGeneration)
								   closeCallbackServers();
						   });
				if (!denied.isEmpty() || code.isEmpty() || !stateOk) {
					emit loginFailed(p, !denied.isEmpty() ? denied
							    : stateOk         ? QStringLiteral("no code in the answer")
									      : QStringLiteral("state mismatch"));
					return;
				}
				const ChatAccount &a = account(p);
				QList<QPair<QString, QString>> form{{QStringLiteral("code"), code},
								    {QStringLiteral("client_id"), a.clientId},
								    {QStringLiteral("redirect_uri"), redirectUri(p)},
								    {QStringLiteral("grant_type"),
								     QStringLiteral("authorization_code")}};
				if (!a.clientSecret.isEmpty())
					form.append({QStringLiteral("client_secret"), a.clientSecret});
				if (p != ChatPlatform::Twitch)
					form.append({QStringLiteral("code_verifier"), QString::fromLatin1(verifier)});
				post(tokenUrl(p), OAuthUtil::formBody(form),
				     [this, p](int, const QJsonObject &body, const QString &error) {
					     if (!error.isEmpty())
						     emit loginFailed(p, error);
					     else
						     finishLogin(p, body);
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
	if (p == ChatPlatform::YouTube) {
		api(p, "GET",
		    youtubeUrl(QStringLiteral("channels"), {{QStringLiteral("part"), QStringLiteral("snippet")},
							    {QStringLiteral("mine"), QStringLiteral("true")}}),
		    QJsonObject(), [this, p](int, const QJsonObject &body, const QString &error) {
			    const QJsonObject channel = body.value(QStringLiteral("items")).toArray().at(0).toObject();
			    ChatAccount &a = acc(p);
			    a.userId = channel.value(QStringLiteral("id")).toString();
			    a.login = channel.value(QStringLiteral("snippet"))
					      .toObject()
					      .value(QStringLiteral("title"))
					      .toString();
			    if (!error.isEmpty() || a.userId.isEmpty()) {
				    a = ChatAccount{a.clientId, a.clientSecret, {}, {}, {}, {}, 0};
				    save();
				    emit loginFailed(
					    p, error.isEmpty()
						       ? QStringLiteral("this Google account has no YouTube channel")
						       : error);
				    emit accountChanged(p);
				    return;
			    }
			    save();
			    emit accountChanged(p);
		    });
		return;
	}
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
	/* A user's own Kick app, or a confidential Twitch app, sends its secret;
	 * the plugin's Kick app gets it added by the token server. */
	if (!a.clientSecret.isEmpty())
		form.append({QStringLiteral("client_secret"), a.clientSecret});
	post(tokenUrl(p), OAuthUtil::formBody(form),
	     [this, p, done](int, const QJsonObject &body, const QString &error) {
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

void ChatAccounts::youtubeBroadcast(bool orUpcoming,
				    std::function<void(const QString &, const QString &, const QString &)> done)
{
	/* The stream being run from this account: live now, else (for title and
	 * category, which you set before going live) the next one; the
	 * always-there "stream now" broadcast shows up as upcoming. Chat goes
	 * only to a live one: an upcoming broadcast's chat is seen by nobody. */
	const auto lookFor = [this, done](const QString &status, std::function<void()> otherwise) {
		api(ChatPlatform::YouTube, "GET",
		    youtubeUrl(QStringLiteral("liveBroadcasts"),
			       {{QStringLiteral("part"), QStringLiteral("id,snippet")},
				{QStringLiteral("broadcastStatus"), status},
				{QStringLiteral("broadcastType"), QStringLiteral("all")},
				{QStringLiteral("maxResults"), QStringLiteral("1")}}),
		    QJsonObject(), [done, otherwise](int, const QJsonObject &body, const QString &error) {
			    const QJsonObject b = body.value(QStringLiteral("items")).toArray().at(0).toObject();
			    if (!error.isEmpty()) {
				    done(QString(), QString(), error);
				    return;
			    }
			    if (b.isEmpty()) {
				    otherwise();
				    return;
			    }
			    done(b.value(QStringLiteral("id")).toString(),
				 b.value(QStringLiteral("snippet"))
					 .toObject()
					 .value(QStringLiteral("liveChatId"))
					 .toString(),
				 QString());
		    });
	};
	lookFor(QStringLiteral("active"), [lookFor, done, orUpcoming]() {
		if (!orUpcoming) {
			done(QString(), QString(), QStringLiteral("you are not live on YouTube right now"));
			return;
		}
		lookFor(QStringLiteral("upcoming"), [done]() {
			done(QString(), QString(),
			     QStringLiteral("no live or upcoming stream on this YouTube channel"));
		});
	});
}

void ChatAccounts::youtubeLiveVideo(const QString &channelId,
				    std::function<void(const QString &, const QString &)> done)
{
	const ChatAccount &a = account(ChatPlatform::YouTube);
	if (!a.loggedIn() || a.userId != channelId) {
		done(QString(), QString());
		return;
	}
	api(ChatPlatform::YouTube, "GET",
	    youtubeUrl(QStringLiteral("liveBroadcasts"), {{QStringLiteral("part"), QStringLiteral("id,status")},
							  {QStringLiteral("broadcastStatus"), QStringLiteral("active")},
							  {QStringLiteral("broadcastType"), QStringLiteral("all")},
							  {QStringLiteral("maxResults"), QStringLiteral("1")}}),
	    QJsonObject(), [done](int, const QJsonObject &body, const QString &) {
		    const QJsonObject b = body.value(QStringLiteral("items")).toArray().at(0).toObject();
		    done(b.value(QStringLiteral("id")).toString(),
			 b.value(QStringLiteral("status")).toObject().value(QStringLiteral("privacyStatus")).toString());
	    });
}

void ChatAccounts::youtubeCategories(std::function<void(const QList<StreamCategory> &, const QString &)> done)
{
	if (!m_youtubeCategories.isEmpty()) {
		done(m_youtubeCategories, QString());
		return;
	}
	/* YouTube only has a fixed list of video categories (no games). */
	const QString region = QLocale::system().name().section(QLatin1Char('_'), 1, 1);
	api(ChatPlatform::YouTube, "GET",
	    youtubeUrl(QStringLiteral("videoCategories"),
		       {{QStringLiteral("part"), QStringLiteral("snippet")},
			{QStringLiteral("regionCode"), region.isEmpty() ? QStringLiteral("US") : region},
			{QStringLiteral("hl"), QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'))}}),
	    QJsonObject(), [this, done](int, const QJsonObject &body, const QString &error) {
		    QList<StreamCategory> found;
		    for (const QJsonValue v : body.value(QStringLiteral("items")).toArray()) {
			    const QJsonObject o = v.toObject();
			    const QJsonObject snippet = o.value(QStringLiteral("snippet")).toObject();
			    if (snippet.value(QStringLiteral("assignable")).toBool())
				    found.append({o.value(QStringLiteral("id")).toString(),
						  snippet.value(QStringLiteral("title")).toString()});
		    }
		    if (error.isEmpty())
			    m_youtubeCategories = found;
		    done(found, error);
	    });
}

void ChatAccounts::sendMessage(ChatPlatform p, const QString &channel, const QString &text)
{
	if (p == ChatPlatform::YouTube) {
		/* Into this account's own live chat, whatever channel the dock reads. */
		youtubeBroadcast(false, [this, text](const QString &, const QString &chatId, const QString &error) {
			if (!error.isEmpty() || chatId.isEmpty()) {
				emit actionFailed(ChatPlatform::YouTube,
						  error.isEmpty() ? QStringLiteral("no live chat") : error);
				return;
			}
			QJsonObject details{{QStringLiteral("messageText"), text}};
			QJsonObject snippet{{QStringLiteral("liveChatId"), chatId},
					    {QStringLiteral("type"), QStringLiteral("textMessageEvent")},
					    {QStringLiteral("textMessageDetails"), details}};
			api(ChatPlatform::YouTube, "POST",
			    youtubeUrl(QStringLiteral("liveChat/messages"),
				       {{QStringLiteral("part"), QStringLiteral("snippet")}}),
			    QJsonObject{{QStringLiteral("snippet"), snippet}},
			    [this](int, const QJsonObject &, const QString &error) {
				    if (!error.isEmpty())
					    emit actionFailed(ChatPlatform::YouTube, error);
			    });
		});
		return;
	}
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

void ChatAccounts::timeoutUser(ChatPlatform p, const QString &channel, const QString &userId, int seconds,
			       ActionDone done)
{
	if (p == ChatPlatform::YouTube) {
		youtubeBroadcast(false, [this, userId, seconds, done](const QString &, const QString &chatId,
								      const QString &error) {
			if (!error.isEmpty() || chatId.isEmpty()) {
				const QString e = error.isEmpty() ? QStringLiteral("no live chat") : error;
				emit actionFailed(ChatPlatform::YouTube, e);
				if (done)
					done(e);
				return;
			}
			QJsonObject snippet{{QStringLiteral("liveChatId"), chatId},
					    {QStringLiteral("type"),
					     seconds > 0 ? QStringLiteral("temporary") : QStringLiteral("permanent")},
					    {QStringLiteral("bannedUserDetails"),
					     QJsonObject{{QStringLiteral("channelId"), userId}}}};
			if (seconds > 0)
				snippet.insert(QStringLiteral("banDurationSeconds"), seconds);
			api(ChatPlatform::YouTube, "POST",
			    youtubeUrl(QStringLiteral("liveChat/bans"),
				       {{QStringLiteral("part"), QStringLiteral("snippet")}}),
			    QJsonObject{{QStringLiteral("snippet"), snippet}},
			    [this, userId, done](int, const QJsonObject &body, const QString &error) {
				    /* YouTube lifts a ban by its id, not by the user. */
				    if (error.isEmpty())
					    m_youtubeBans.insert(userId, body.value(QStringLiteral("id")).toString());
				    else
					    emit actionFailed(ChatPlatform::YouTube, error);
				    if (done)
					    done(error);
			    });
		});
		return;
	}
	withBroadcaster(p, channel, [this, p, userId, seconds, done](const QString &broadcaster) {
		const auto report = [this, p, done](int, const QJsonObject &, const QString &error) {
			if (!error.isEmpty())
				emit actionFailed(p, error);
			if (done)
				done(error);
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

void ChatAccounts::banUser(ChatPlatform p, const QString &channel, const QString &userId, ActionDone done)
{
	timeoutUser(p, channel, userId, 0, std::move(done));
}

void ChatAccounts::unbanUser(ChatPlatform p, const QString &channel, const QString &userId, ActionDone done)
{
	if (p == ChatPlatform::YouTube) {
		const QString banId = m_youtubeBans.value(userId);
		if (banId.isEmpty()) {
			const QString e = QStringLiteral(
				"YouTube only lifts bans made from this OBS since it opened; use YouTube Studio for older ones");
			emit actionFailed(p, e);
			if (done)
				done(e);
			return;
		}
		api(p, "DELETE", youtubeUrl(QStringLiteral("liveChat/bans"), {{QStringLiteral("id"), banId}}),
		    QJsonObject(), [this, p, userId, done](int, const QJsonObject &, const QString &error) {
			    if (error.isEmpty())
				    m_youtubeBans.remove(userId);
			    else
				    emit actionFailed(p, error);
			    if (done)
				    done(error);
		    });
		return;
	}
	withBroadcaster(p, channel, [this, p, userId, done](const QString &broadcaster) {
		const auto report = [this, p, done](int, const QJsonObject &, const QString &error) {
			if (!error.isEmpty())
				emit actionFailed(p, error);
			if (done)
				done(error);
		};
		if (p == ChatPlatform::Twitch) {
			QUrl url(QStringLiteral("https://api.twitch.tv/helix/moderation/bans"));
			url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
					       {QStringLiteral("moderator_id"), account(p).userId},
					       {QStringLiteral("user_id"), userId}});
			api(p, "DELETE", url, QJsonObject(), report);
		} else {
			api(p, "DELETE", QUrl(QStringLiteral("https://api.kick.com/public/v1/moderation/bans")),
			    QJsonObject{{QStringLiteral("broadcaster_user_id"), broadcaster.toLongLong()},
					{QStringLiteral("user_id"), userId.toLongLong()}},
			    report);
		}
	});
}

void ChatAccounts::deleteMessage(ChatPlatform p, const QString &channel, const QString &messageId)
{
	const auto report = [this, p](int, const QJsonObject &, const QString &error) {
		if (!error.isEmpty())
			emit actionFailed(p, error);
	};
	if (p == ChatPlatform::YouTube) {
		api(p, "DELETE", youtubeUrl(QStringLiteral("liveChat/messages"), {{QStringLiteral("id"), messageId}}),
		    QJsonObject(), report);
		return;
	}
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

void ChatAccounts::helixPages(const QUrl &url, int maxPages,
			      std::function<void(const QJsonArray &, const QString &)> done, QJsonArray collected,
			      const QString &cursor)
{
	QUrl page = url;
	QUrlQuery query(url);
	if (!cursor.isEmpty())
		query.addQueryItem(QStringLiteral("after"), cursor);
	page.setQuery(query);
	api(ChatPlatform::Twitch, "GET", page, QJsonObject(),
	    [this, url, maxPages, done, collected](int, const QJsonObject &body, const QString &error) mutable {
		    if (!error.isEmpty()) {
			    done(collected, error);
			    return;
		    }
		    for (const QJsonValue v : body.value(QStringLiteral("data")).toArray())
			    collected.append(v);
		    const QString next = body.value(QStringLiteral("pagination"))
						 .toObject()
						 .value(QStringLiteral("cursor"))
						 .toString();
		    if (next.isEmpty() || maxPages <= 1)
			    done(collected, QString());
		    else
			    helixPages(url, maxPages - 1, done, collected, next);
	    });
}

void ChatAccounts::twitchChatterList(const QString &channel, UsersDone done)
{
	withBroadcaster(ChatPlatform::Twitch, channel, [this, done](const QString &broadcaster) {
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/chatters"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
				       {QStringLiteral("moderator_id"), account(ChatPlatform::Twitch).userId},
				       {QStringLiteral("first"), QStringLiteral("1000")}});
		helixPages(url, 10, [done](const QJsonArray &data, const QString &error) {
			QList<ChatUser> users;
			for (const QJsonValue v : data) {
				const QJsonObject o = v.toObject();
				users.append({o.value(QStringLiteral("user_id")).toString(),
					      o.value(QStringLiteral("user_login")).toString(),
					      o.value(QStringLiteral("user_name")).toString(),
					      {},
					      {},
					      {}});
			}
			done(users, error);
		});
	});
}

void ChatAccounts::twitchBanList(const QString &channel, UsersDone done)
{
	withBroadcaster(ChatPlatform::Twitch, channel, [this, done](const QString &broadcaster) {
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/moderation/banned"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), broadcaster},
				       {QStringLiteral("first"), QStringLiteral("100")}});
		helixPages(url, 20, [done](const QJsonArray &data, const QString &error) {
			QList<ChatUser> users;
			for (const QJsonValue v : data) {
				const QJsonObject o = v.toObject();
				users.append({o.value(QStringLiteral("user_id")).toString(),
					      o.value(QStringLiteral("user_login")).toString(),
					      o.value(QStringLiteral("user_name")).toString(),
					      QDateTime::fromString(o.value(QStringLiteral("expires_at")).toString(),
								    Qt::ISODate),
					      o.value(QStringLiteral("reason")).toString(),
					      o.value(QStringLiteral("moderator_name")).toString()});
			}
			done(users, error);
		});
	});
}

void ChatAccounts::streamInfo(ChatPlatform p, std::function<void(const StreamInfo &, const QString &)> done)
{
	/* Always the logged-in account's own channel: that is the stream you run. */
	if (p == ChatPlatform::YouTube) {
		youtubeBroadcast(true, [this, done](const QString &videoId, const QString &, const QString &error) {
			if (!error.isEmpty()) {
				done({}, error);
				return;
			}
			api(ChatPlatform::YouTube, "GET",
			    youtubeUrl(QStringLiteral("videos"), {{QStringLiteral("part"), QStringLiteral("snippet")},
								  {QStringLiteral("id"), videoId}}),
			    QJsonObject(), [this, done](int, const QJsonObject &body, const QString &error) {
				    const QJsonObject snippet = body.value(QStringLiteral("items"))
									.toArray()
									.at(0)
									.toObject()
									.value(QStringLiteral("snippet"))
									.toObject();
				    const QString categoryId = snippet.value(QStringLiteral("categoryId")).toString();
				    const QString title = snippet.value(QStringLiteral("title")).toString();
				    youtubeCategories([done, error, title, categoryId](const QList<StreamCategory> &all,
										       const QString &) {
					    QString name;
					    for (const StreamCategory &c : all)
						    if (c.id == categoryId)
							    name = c.name;
					    done({title, categoryId, name}, error);
				    });
			    });
		});
		return;
	}
	if (p == ChatPlatform::Twitch) {
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/channels"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), account(p).userId}});
		api(p, "GET", url, QJsonObject(), [done](int, const QJsonObject &body, const QString &error) {
			const QJsonObject c = body.value(QStringLiteral("data")).toArray().at(0).toObject();
			done({c.value(QStringLiteral("title")).toString(),
			      c.value(QStringLiteral("game_id")).toString(),
			      c.value(QStringLiteral("game_name")).toString()},
			     error);
		});
		return;
	}
	/* Without a slug or id Kick answers with the token owner's channel. */
	api(p, "GET", QUrl(QStringLiteral("https://api.kick.com/public/v1/channels")), QJsonObject(),
	    [done](int, const QJsonObject &body, const QString &error) {
		    const QJsonObject c = body.value(QStringLiteral("data")).toArray().at(0).toObject();
		    const QJsonObject cat = c.value(QStringLiteral("category")).toObject();
		    const qint64 id = cat.value(QStringLiteral("id")).toInteger();
		    done({c.value(QStringLiteral("stream_title")).toString(), id > 0 ? QString::number(id) : QString(),
			  cat.value(QStringLiteral("name")).toString()},
			 error);
	    });
}

void ChatAccounts::searchCategories(ChatPlatform p, const QString &query,
				    std::function<void(const QList<StreamCategory> &, const QString &)> done)
{
	if (p == ChatPlatform::YouTube) {
		youtubeCategories([query, done](const QList<StreamCategory> &all, const QString &error) {
			QList<StreamCategory> found;
			for (const StreamCategory &c : all)
				if (c.name.contains(query, Qt::CaseInsensitive))
					found.append(c);
			done(found, error);
		});
		return;
	}
	QUrl url(p == ChatPlatform::Twitch ? QStringLiteral("https://api.twitch.tv/helix/search/categories")
					   : QStringLiteral("https://api.kick.com/public/v2/categories"));
	url.setQuery(
		p == ChatPlatform::Twitch
			? QUrlQuery{{QStringLiteral("query"), query}, {QStringLiteral("first"), QStringLiteral("25")}}
			: QUrlQuery{{QStringLiteral("name"), query}, {QStringLiteral("limit"), QStringLiteral("25")}});
	api(p, "GET", url, QJsonObject(), [done](int, const QJsonObject &body, const QString &error) {
		QList<StreamCategory> found;
		for (const QJsonValue v : body.value(QStringLiteral("data")).toArray()) {
			const QJsonObject o = v.toObject();
			const QJsonValue id = o.value(QStringLiteral("id"));
			found.append({id.isString() ? id.toString() : QString::number(id.toInteger()),
				      o.value(QStringLiteral("name")).toString()});
		}
		done(found, error);
	});
}

void ChatAccounts::updateStreamInfo(ChatPlatform p, const QString &title, const QString &categoryId, ActionDone done)
{
	const auto report = [done](int, const QJsonObject &, const QString &error) {
		done(error);
	};
	if (p == ChatPlatform::YouTube) {
		/* videos.update replaces the whole snippet: send back what is there,
		 * with the new title and category. */
		youtubeBroadcast(true, [this, title, categoryId, done](const QString &videoId, const QString &,
								       const QString &error) {
			if (!error.isEmpty()) {
				done(error);
				return;
			}
			api(ChatPlatform::YouTube, "GET",
			    youtubeUrl(QStringLiteral("videos"), {{QStringLiteral("part"), QStringLiteral("snippet")},
								  {QStringLiteral("id"), videoId}}),
			    QJsonObject(),
			    [this, videoId, title, categoryId, done](int, const QJsonObject &body,
								     const QString &error) {
				    if (!error.isEmpty()) {
					    done(error);
					    return;
				    }
				    const QJsonObject old = body.value(QStringLiteral("items"))
								    .toArray()
								    .at(0)
								    .toObject()
								    .value(QStringLiteral("snippet"))
								    .toObject();
				    QJsonObject snippet;
				    for (const char *key :
					 {"title", "categoryId", "description", "tags", "defaultLanguage"}) {
					    if (old.contains(QLatin1String(key)))
						    snippet.insert(QLatin1String(key), old.value(QLatin1String(key)));
				    }
				    if (!title.isEmpty())
					    snippet.insert(QStringLiteral("title"), title);
				    if (!categoryId.isEmpty())
					    snippet.insert(QStringLiteral("categoryId"), categoryId);
				    api(ChatPlatform::YouTube, "PUT",
					youtubeUrl(QStringLiteral("videos"),
						   {{QStringLiteral("part"), QStringLiteral("snippet")}}),
					QJsonObject{{QStringLiteral("id"), videoId},
						    {QStringLiteral("snippet"), snippet}},
					[done](int, const QJsonObject &, const QString &error) { done(error); });
			    });
		});
		return;
	}
	if (p == ChatPlatform::Twitch) {
		QUrl url(QStringLiteral("https://api.twitch.tv/helix/channels"));
		url.setQuery(QUrlQuery{{QStringLiteral("broadcaster_id"), account(p).userId}});
		QJsonObject body;
		if (!title.isEmpty())
			body.insert(QStringLiteral("title"), title);
		if (!categoryId.isEmpty())
			body.insert(QStringLiteral("game_id"), categoryId);
		api(p, "PATCH", url, body, report);
		return;
	}
	QJsonObject body;
	if (!title.isEmpty())
		body.insert(QStringLiteral("stream_title"), title);
	if (!categoryId.isEmpty())
		body.insert(QStringLiteral("category_id"), categoryId.toLongLong());
	api(p, "PATCH", QUrl(QStringLiteral("https://api.kick.com/public/v1/channels")), body, report);
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
