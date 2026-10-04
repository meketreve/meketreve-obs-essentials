/*
Meketreve OBS Essentials - Now Playing
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

#include "media-info.hpp"
#include "spectrum.hpp"

#include "../texuguito/overlay-server.hpp"

#include <obs.h>

#include <QThreadPool>
#include <QTimer>
#include <QObject>

#include <memory>

/* "Now playing" overlay: the bars of an OBS audio source plus the song the
 * computer is playing, on a Browser Source served at localhost:8903. The
 * audio is only captured while a page is connected. */
class NowPlayingDock : public QObject {
	Q_OBJECT

public:
	explicit NowPlayingDock(QObject *parent = nullptr);
	~NowPlayingDock() override;

	/* The web panel's tab: the audio sources to pick from, the look and the
	 * overlay link; applyPanel takes what the panel saved and answers the
	 * new state. */
	QJsonObject panelState();
	QJsonObject applyPanel(const QJsonObject &panel);
	QString overlayUrl() const;
	void addBrowserSource();

private:
	void loadSettings();
	void saveSettings();
	static QStringList audioSourceNames();
	void setSource(const QString &name);
	void onClients(int count);
	void startCapture();
	void stopCapture();
	void sendBands();
	void pollMedia();
	void onMedia(const MediaInfo &media);
	QJsonObject mediaMessage() const;
	bool route(const OverlayServer::Request &request, OverlayServer::Reply &reply);

	static void audioCallback(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);

	QString m_dir;
	quint16 m_port = 8903;
	QString m_sourceName; /* empty = the Desktop Audio of the OBS settings */
	/* The look; "?color=", "?card=0"... in the link still win, per source. */
	QString m_color = QStringLiteral("#3987E5");
	bool m_card = true;
	bool m_bars = true;
	bool m_always = false;

	OverlayServer *m_server = nullptr;
	std::unique_ptr<Spectrum> m_spectrum;
	obs_weak_source_t *m_source = nullptr;
	size_t m_channels = 2;
	QTimer m_frameTimer;
	QTimer m_mediaTimer;
	QThreadPool m_pool;
	bool m_mediaBusy = false;
	MediaInfo m_media;
	QString m_artFile;
};
