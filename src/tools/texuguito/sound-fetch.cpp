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

#include "sound-fetch.hpp"

#include "../unified-chat/chat-connector.hpp"

#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>

#include <memory>

namespace SoundFetch {

namespace {

bool privateAddress(const QHostAddress &a)
{
	if (a.isLoopback() || a.isLinkLocal() || a.isNull() || a == QHostAddress(QHostAddress::AnyIPv4) ||
	    a == QHostAddress(QHostAddress::AnyIPv6))
		return true;
	static const QList<std::pair<QHostAddress, int>> ranges = {
		QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
		QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
		QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
		QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")),
		QHostAddress::parseSubnet(QStringLiteral("fc00::/7")),
	};
	for (const auto &range : ranges) {
		if (a.isInSubnet(range))
			return true;
	}
	return false;
}

using GetDone = std::function<void(QByteArray body, QString error, bool tooBig, int status)>;

void get(QNetworkAccessManager *net, const QUrl &url, qint64 maxBytes, QObject *context, GetDone done)
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	req.setRawHeader("Accept", "text/html,audio/*,*/*;q=0.8");
	req.setTransferTimeout(20000);
	QNetworkReply *reply = net->get(req);
	auto tooBig = std::make_shared<bool>(false);
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
			 [reply, maxBytes, tooBig](qint64 received, qint64 total) {
				 if (received > maxBytes || total > maxBytes) {
					 *tooBig = true;
					 reply->abort();
				 }
			 });
	QPointer<QObject> guard = context;
	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, tooBig, guard, done]() {
		reply->deleteLater();
		if (!guard)
			return;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		/* Error pages still have a body (Cloudflare says why it blocked). */
		const QByteArray body = *tooBig ? QByteArray() : reply->readAll();
		const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
		done(body, error, *tooBig, status);
	});
}

bool blockedPage(int status, const QByteArray &body)
{
	return (status == 403 || status == 429 || status == 503) &&
	       (body.contains("cloudflare") || body.contains("Cloudflare") || body.contains("challenge-platform"));
}

Result audioResult(const QByteArray &body, const QUrl &audioUrl)
{
	Result r;
	r.ext = audioType(body.left(16));
	if (r.ext.isEmpty()) {
		r.error = Error::NoAudio;
		return r;
	}
	r.data = body;
	r.name = nameFromUrl(audioUrl);
	return r;
}

Result failure(Error error, const QString &detail = QString())
{
	Result r;
	r.error = error;
	r.detail = detail;
	return r;
}

} // namespace

bool allowedUrl(const QUrl &url)
{
	if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty())
		return false;
	const QString host = url.host().toLower();
	if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost")) ||
	    host.endsWith(QLatin1String(".local")))
		return false;
	QHostAddress address;
	if (address.setAddress(host) && privateAddress(address))
		return false;
	return true;
}

QString audioType(const QByteArray &head)
{
	if (head.startsWith("ID3"))
		return QStringLiteral("mp3");
	/* An MPEG audio frame starts with 11 set bits. */
	if (head.size() >= 2 && static_cast<unsigned char>(head[0]) == 0xFF &&
	    (static_cast<unsigned char>(head[1]) & 0xE0) == 0xE0)
		return QStringLiteral("mp3");
	if (head.startsWith("OggS"))
		return QStringLiteral("ogg");
	if (head.size() >= 12 && head.startsWith("RIFF") && head.mid(8, 4) == "WAVE")
		return QStringLiteral("wav");
	return QString();
}

QUrl audioUrlInPage(const QByteArray &html, const QUrl &page)
{
	const QString text = QString::fromUtf8(html);
	static const QRegularExpression ogAudio(
		QStringLiteral("<meta[^>]+property=[\"']og:audio(?::url)?[\"'][^>]+content=[\"']([^\"']+)[\"']"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression anyLink(
		QStringLiteral("[\"'(]([^\"'()\\s<>]+\\.(?:mp3|ogg|wav))(?:\\?[^\"'()\\s<>]*)?[\"')]"),
		QRegularExpression::CaseInsensitiveOption);
	for (const QRegularExpression *re : {&ogAudio, &anyLink}) {
		const QRegularExpressionMatch m = re->match(text);
		if (m.hasMatch()) {
			const QUrl found = page.resolved(QUrl(m.captured(1).replace(QLatin1String("&amp;"), "&")));
			if (found.isValid())
				return found;
		}
	}
	return QUrl();
}

QString clipName(const QString &raw)
{
	const QString decomposed = raw.normalized(QString::NormalizationForm_KD).toLower();
	QString out;
	for (const QChar c : decomposed) {
		if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
		    (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_'))
			out.append(c);
		else if (c.category() == QChar::Mark_NonSpacing)
			continue; /* the accent of "á" */
		else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
			out.append(QLatin1Char('-'));
	}
	while (out.endsWith(QLatin1Char('-')))
		out.chop(1);
	if (out.size() > 32) {
		out.truncate(32);
		while (out.endsWith(QLatin1Char('-')))
			out.chop(1);
	}
	return out;
}

QString nameFromUrl(const QUrl &url)
{
	return clipName(QFileInfo(url.path()).completeBaseName());
}

void fetch(QNetworkAccessManager *net, const QString &link, std::function<void(const Result &)> done, QObject *context)
{
	const QUrl url = QUrl::fromUserInput(link.trimmed());
	if (!allowedUrl(url)) {
		done(failure(Error::BadLink));
		return;
	}
	get(net, url, kMaxAudioBytes, context,
	    [net, url, done, context](QByteArray body, QString error, bool tooBig, int status) {
		    if (tooBig) {
			    done(failure(Error::TooBig));
			    return;
		    }
		    if (blockedPage(status, body)) {
			    done(failure(Error::Blocked, url.host()));
			    return;
		    }
		    if (!error.isEmpty()) {
			    done(failure(Error::Network, error));
			    return;
		    }
		    /* A direct audio link: done. */
		    if (!audioType(body.left(16)).isEmpty()) {
			    done(audioResult(body, url));
			    return;
		    }
		    /* A page: follow the sound it links, once. */
		    const QUrl audioUrl = audioUrlInPage(body, url);
		    if (!allowedUrl(audioUrl)) {
			    done(failure(Error::NoAudio));
			    return;
		    }
		    get(net, audioUrl, kMaxAudioBytes, context,
			[audioUrl, done](QByteArray audio, QString audioError, bool audioTooBig, int audioStatus) {
				if (audioTooBig)
					done(failure(Error::TooBig));
				else if (blockedPage(audioStatus, audio))
					done(failure(Error::Blocked, audioUrl.host()));
				else if (!audioError.isEmpty())
					done(failure(Error::Network, audioError));
				else
					done(audioResult(audio, audioUrl));
			});
	    });
}

} // namespace SoundFetch
