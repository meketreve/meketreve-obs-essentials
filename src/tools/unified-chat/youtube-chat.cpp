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

#include "youtube-chat.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <algorithm>

namespace {

constexpr int kOfflineRecheckSeconds = 60;

QJsonValue path(QJsonValue v, std::initializer_list<const char *> keys)
{
	for (const char *key : keys)
		v = v.toObject().value(QLatin1String(key));
	return v;
}

QString runsToText(const QJsonArray &runs)
{
	QString out;
	for (const QJsonValue &run : runs) {
		const QJsonObject r = run.toObject();
		if (r.contains(QStringLiteral("text"))) {
			out += r.value(QStringLiteral("text")).toString();
			continue;
		}
		const QJsonObject emoji = r.value(QStringLiteral("emoji")).toObject();
		const QJsonArray shortcuts = emoji.value(QStringLiteral("shortcuts")).toArray();
		if (emoji.value(QStringLiteral("isCustomEmoji")).toBool() && !shortcuts.isEmpty())
			out += shortcuts.first().toString();
		else
			out += emoji.value(QStringLiteral("emojiId")).toString();
	}
	return out;
}

/* ytInitialData is a JS object literal embedded in the page. */
QJsonObject extractInitialData(const QByteArray &html)
{
	const QByteArray marker = "ytInitialData\"] = ";
	qsizetype start = html.indexOf(marker);
	if (start < 0) {
		start = html.indexOf("var ytInitialData = ");
		if (start < 0)
			return {};
		start += 20;
	} else {
		start += marker.size();
	}

	int depth = 0;
	bool inString = false;
	for (qsizetype i = start; i < html.size(); i++) {
		const char c = html[i];
		if (inString) {
			if (c == '\\')
				i++;
			else if (c == '"')
				inString = false;
			continue;
		}
		if (c == '"')
			inString = true;
		else if (c == '{')
			depth++;
		else if (c == '}' && --depth == 0)
			return QJsonDocument::fromJson(html.mid(start, i - start + 1)).object();
	}
	return {};
}

QString regexCapture(const QByteArray &html, const QString &pattern)
{
	const QRegularExpression re(pattern);
	return re.match(QString::fromUtf8(html)).captured(1);
}

} // namespace

YouTubeChat::YouTubeChat(QNetworkAccessManager *net, QObject *parent)
	: ChatConnector(ChatPlatform::YouTube, net, parent)
{
	m_pollTimer.setSingleShot(true);
	connect(&m_pollTimer, &QTimer::timeout, this, &YouTubeChat::poll);
}

QString YouTubeChat::videoIdFromInput(const QString &input)
{
	const QString s = input.trimmed();
	static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
	if (idRe.match(s).hasMatch())
		return s;
	static const QRegularExpression urlRe(
		QStringLiteral("(?:[?&]v=|youtu\\.be/|/live/|/shorts/)([A-Za-z0-9_-]{11})(?![A-Za-z0-9_-])"));
	return urlRe.match(s).captured(1);
}

QString YouTubeChat::liveUrlFromInput(const QString &input)
{
	QString s = input.trimmed();
	static const QRegularExpression channelRe(
		QStringLiteral("youtube\\.com/((?:@[^/?#]+)|(?:channel/[^/?#]+)|(?:c/[^/?#]+)|(?:user/[^/?#]+))"));
	const auto m = channelRe.match(s);
	if (m.hasMatch())
		s = m.captured(1);
	else if (!s.startsWith(QLatin1Char('@')) && !s.startsWith(QStringLiteral("UC")))
		s.prepend(QLatin1Char('@'));
	else if (s.startsWith(QStringLiteral("UC")))
		s.prepend(QStringLiteral("channel/"));
	return QStringLiteral("https://www.youtube.com/%1/live").arg(s);
}

QNetworkReply *YouTubeChat::get(const QUrl &url)
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	/* Skips the EU cookie-consent interstitial. */
	req.setRawHeader("Cookie", "SOCS=CAI");
	req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
	req.setTransferTimeout(20000);
	m_pending = net()->get(req);
	return m_pending;
}

void YouTubeChat::connectNow()
{
	setState(ConnectorState::Connecting);
	const QString videoId = videoIdFromInput(target());
	if (!videoId.isEmpty()) {
		loadChatPage(videoId);
		return;
	}
	QNetworkReply *reply = get(QUrl(liveUrlFromInput(target())));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onLivePage(reply); });
}

void YouTubeChat::disconnectNow()
{
	m_pollTimer.stop();
	if (m_pending) {
		m_pending->abort();
		m_pending = nullptr;
	}
	m_continuation.clear();
}

void YouTubeChat::onLivePage(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	if (reply->error() != QNetworkReply::NoError) {
		setState(ConnectorState::Error, reply->errorString());
		scheduleReconnect();
		return;
	}

	/* /live redirects to the channel page when nothing is on air. */
	const QString videoId = regexCapture(
		reply->readAll(),
		QStringLiteral(
			"<link rel=\"canonical\" href=\"https://www\\.youtube\\.com/watch\\?v=([A-Za-z0-9_-]{11})\""));
	if (videoId.isEmpty()) {
		setState(ConnectorState::Offline);
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}
	loadChatPage(videoId);
}

void YouTubeChat::loadChatPage(const QString &videoId)
{
	QNetworkReply *reply =
		get(QUrl(QStringLiteral("https://www.youtube.com/live_chat?is_popout=1&v=%1").arg(videoId)));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onChatPage(reply); });
}

void YouTubeChat::onChatPage(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	if (reply->error() != QNetworkReply::NoError) {
		setState(ConnectorState::Error, reply->errorString());
		scheduleReconnect();
		return;
	}

	const QByteArray html = reply->readAll();
	m_apiKey = regexCapture(html, QStringLiteral("\"INNERTUBE_API_KEY\":\"([^\"]+)\""));
	m_clientVersion = regexCapture(html, QStringLiteral("\"INNERTUBE_CLIENT_VERSION\":\"([^\"]+)\""));

	const QJsonObject data = extractInitialData(html);
	const QJsonObject renderer = path(data, {"contents", "liveChatRenderer"}).toObject();

	/* Prefer "Live chat" (every message) over the default "Top chat". */
	const QJsonArray views = path(renderer, {"header", "liveChatHeaderRenderer", "viewSelector",
						 "sortFilterSubMenuRenderer", "subMenuItems"})
					 .toArray();
	m_continuation =
		path(views.at(views.size() - 1), {"continuation", "reloadContinuationData", "continuation"}).toString();
	if (m_continuation.isEmpty()) {
		const QJsonObject first = renderer.value(QStringLiteral("continuations")).toArray().at(0).toObject();
		for (const QString &key : first.keys()) {
			m_continuation = path(first.value(key), {"continuation"}).toString();
			if (!m_continuation.isEmpty())
				break;
		}
	}

	if (m_continuation.isEmpty() || m_clientVersion.isEmpty()) {
		/* Chat disabled, members-only, or the stream already ended. */
		setState(ConnectorState::Offline);
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}

	/* The first response replays recent history; skip it so a reconnect
	 * does not duplicate messages already shown. */
	m_skipBacklog = true;
	markHealthy();
	poll();
}

void YouTubeChat::poll()
{
	if (!running() || m_continuation.isEmpty())
		return;

	QString url = QStringLiteral("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?prettyPrint=false");
	if (!m_apiKey.isEmpty())
		url += QStringLiteral("&key=") + m_apiKey;

	QNetworkRequest req{QUrl(url)};
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	req.setTransferTimeout(20000);

	const QJsonObject body{{QStringLiteral("context"),
				QJsonObject{{QStringLiteral("client"),
					     QJsonObject{{QStringLiteral("clientName"), QStringLiteral("WEB")},
							 {QStringLiteral("clientVersion"), m_clientVersion}}}}},
			       {QStringLiteral("continuation"), m_continuation}};

	m_pending = net()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	QNetworkReply *reply = m_pending;
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { onPoll(reply); });
}

void YouTubeChat::onPoll(QNetworkReply *reply)
{
	reply->deleteLater();
	if (m_pending != reply)
		return;
	m_pending = nullptr;

	if (reply->error() != QNetworkReply::NoError) {
		setState(ConnectorState::Error, reply->errorString());
		scheduleReconnect();
		return;
	}

	const QJsonObject live = path(QJsonDocument::fromJson(reply->readAll()).object(),
				      {"continuationContents", "liveChatContinuation"})
					 .toObject();
	const QJsonObject next = live.value(QStringLiteral("continuations")).toArray().at(0).toObject();

	int timeoutMs = 0;
	m_continuation.clear();
	for (const QString &key : next.keys()) {
		const QJsonObject c = next.value(key).toObject();
		m_continuation = c.value(QStringLiteral("continuation")).toString();
		timeoutMs = c.value(QStringLiteral("timeoutMs")).toInt();
		if (!m_continuation.isEmpty())
			break;
	}

	if (m_continuation.isEmpty()) {
		setState(ConnectorState::Offline);
		scheduleRetry(kOfflineRecheckSeconds);
		return;
	}

	if (!m_skipBacklog) {
		for (const QJsonValue &action : live.value(QStringLiteral("actions")).toArray())
			handleAction(action.toObject());
	}
	m_skipBacklog = false;

	/* The suggested interval is up to 10 s; polling a bit faster keeps the
	 * chat from arriving in visible bursts. */
	m_pollTimer.start(std::clamp(timeoutMs, 1000, 2500));
}

void YouTubeChat::handleAction(const QJsonObject &action)
{
	const QJsonObject item = path(action, {"addChatItemAction", "item"}).toObject();

	const QJsonObject text = item.value(QStringLiteral("liveChatTextMessageRenderer")).toObject();
	if (!text.isEmpty()) {
		emitMessage(path(text, {"authorName", "simpleText"}).toString(), QString(),
			    runsToText(path(text, {"message", "runs"}).toArray()));
		return;
	}

	const QJsonObject paid = item.value(QStringLiteral("liveChatPaidMessageRenderer")).toObject();
	if (!paid.isEmpty()) {
		emitMessage(path(paid, {"authorName", "simpleText"}).toString(), QString(),
			    runsToText(path(paid, {"message", "runs"}).toArray()),
			    path(paid, {"purchaseAmountText", "simpleText"}).toString());
		return;
	}

	const QJsonObject member = item.value(QStringLiteral("liveChatMembershipItemRenderer")).toObject();
	if (!member.isEmpty()) {
		emitMessage(path(member, {"authorName", "simpleText"}).toString(), QString(),
			    runsToText(path(member, {"headerSubtext", "runs"}).toArray()), QString(QChar(0x2605)));
	}
}
