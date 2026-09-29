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
#include "chat-view.hpp"
#include "chat-connector.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <cmath>

namespace {

constexpr qint64 kMaxImageBytes = 2 * 1024 * 1024;

bool isWeb(const QUrl &url)
{
	return url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
}

} // namespace

EmoteImages *EmoteImages::instance()
{
	static QPointer<EmoteImages> images;
	if (!images)
		images = new EmoteImages(qApp);
	return images;
}

EmoteImages::EmoteImages(QObject *parent) : QObject(parent) {}

QImage EmoteImages::image(const QUrl &url, int height)
{
	const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
	const QString key =
		url.toString() + QLatin1Char('@') + QString::number(height) + QLatin1Char('x') + QString::number(dpr);
	const auto scaled = m_scaled.constFind(key);
	if (scaled != m_scaled.constEnd())
		return scaled.value();

	const auto full = m_images.constFind(url);
	if (full != m_images.constEnd()) {
		if (full->isNull())
			return QImage();
		/* Scaled once here, smoothly: the view would scale it on every
		 * paint without filtering. */
		QImage out =
			full->scaledToHeight(static_cast<int>(std::lround(height * dpr)), Qt::SmoothTransformation);
		out.setDevicePixelRatio(dpr);
		m_scaled.insert(key, out);
		return out;
	}

	if (!m_loading.contains(url)) {
		m_loading.insert(url);
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
		QNetworkReply *reply = m_net.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
			reply->deleteLater();
			m_loading.remove(url);
			QImage img;
			if (reply->error() == QNetworkReply::NoError)
				img = QImage::fromData(reply->read(kMaxImageBytes));
			m_images.insert(url, img);
			emit ready(url);
		});
	}
	return QImage();
}

ChatView::ChatView(QWidget *parent) : QTextBrowser(parent)
{
	setOpenLinks(false);
	connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
		if (isWeb(url))
			QDesktopServices::openUrl(url);
	});
	connect(EmoteImages::instance(), &EmoteImages::ready, this, &ChatView::onReady);
	/* Many emotes arrive together: lay the text out once for all of them. */
	m_relayout.setSingleShot(true);
	m_relayout.setInterval(50);
	connect(&m_relayout, &QTimer::timeout, this, [this]() {
		QTextDocument *doc = document();
		doc->markContentsDirty(0, doc->characterCount());
		viewport()->update();
	});
}

int ChatView::emoteHeight() const
{
	return std::max(20, static_cast<int>(std::lround(fontMetrics().height() * 1.6)));
}

QVariant ChatView::loadResource(int type, const QUrl &name)
{
	if (type != QTextDocument::ImageResource || !isWeb(name))
		return QTextBrowser::loadResource(type, name);
	const QImage img = EmoteImages::instance()->image(name, emoteHeight());
	if (!img.isNull())
		return img;
	/* Room for the emote until it arrives (or stays empty if it fails). */
	m_waiting.insert(name);
	const qreal dpr = devicePixelRatioF();
	QImage blank(static_cast<int>(std::lround(emoteHeight() * dpr)),
		     static_cast<int>(std::lround(emoteHeight() * dpr)), QImage::Format_ARGB32_Premultiplied);
	blank.fill(Qt::transparent);
	blank.setDevicePixelRatio(dpr);
	return blank;
}

void ChatView::onReady(const QUrl &url)
{
	if (!m_waiting.remove(url))
		return;
	const QImage img = EmoteImages::instance()->image(url, emoteHeight());
	if (img.isNull())
		return;
	document()->addResource(QTextDocument::ImageResource, url, img);
	m_relayout.start();
}
