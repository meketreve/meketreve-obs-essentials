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

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>

/* Emote pictures, downloaded once and shared by every chat view. */
class EmoteImages : public QObject {
	Q_OBJECT

public:
	static EmoteImages *instance();

	/* The picture scaled to height (logical pixels), or a null image while
	 * it downloads (ready() follows). */
	QImage image(const QUrl &url, int height);

signals:
	void ready(const QUrl &url);

private:
	explicit EmoteImages(QObject *parent);

	QNetworkAccessManager m_net;
	QHash<QUrl, QImage> m_images; /* full size; null = failed */
	QHash<QString, QImage> m_scaled;
	QSet<QUrl> m_loading;
};

/* The chat's text view: shows emotes from http(s) <img> tags and opens
 * http(s) links in the web browser. */
class ChatView : public QTextBrowser {
	Q_OBJECT

public:
	explicit ChatView(QWidget *parent = nullptr);

	/* Emote height that fits the chat font. */
	int emoteHeight() const;

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	void onReady(const QUrl &url);

	QSet<QUrl> m_waiting;
	QTimer m_relayout;
};
