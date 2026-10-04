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

#include "now-playing-dock.hpp"
#include "now-playing.h"
#include "../alerts/alerts-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kFrameMs = 33;
constexpr int kMediaMs = 2000;
constexpr qint64 kMaxArt = 10 * 1024 * 1024;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString moduleConfigDir()
{
	char *dir = obs_module_config_path("now-playing");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	QDir().mkpath(path);
	return path;
}

QString webFile(const char *name)
{
	char *dir = obs_module_file("now-playing/web");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	return QDir(path).filePath(QString::fromLatin1(name));
}

/* Players give art without an extension (Firefox, Chrome), so look at the
 * bytes. */
QByteArray imageType(const QByteArray &data)
{
	if (data.startsWith("\x89PNG"))
		return "image/png";
	if (data.startsWith("\xFF\xD8\xFF"))
		return "image/jpeg";
	if (data.startsWith("GIF8"))
		return "image/gif";
	if (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
		return "image/webp";
	return {};
}

} // namespace

NowPlayingDock::NowPlayingDock(QObject *parent) : QObject(parent)
{
	m_dir = moduleConfigDir();
	loadSettings();

	audio_t *audio = obs_get_audio();
	m_spectrum = std::make_unique<Spectrum>(audio ? static_cast<int>(audio_output_get_sample_rate(audio)) : 48000);
	m_pool.setMaxThreadCount(1);

	OverlayServer::Routes routes;
	routes.snapshot = [this]() {
		return mediaMessage();
	};
	routes.handler = [this](const OverlayServer::Request &request, OverlayServer::Reply &reply) {
		return route(request, reply);
	};
	m_server = new OverlayServer(routes, this);
	connect(m_server, &OverlayServer::clientsChanged, this, &NowPlayingDock::onClients);

	m_frameTimer.setInterval(kFrameMs);
	connect(&m_frameTimer, &QTimer::timeout, this, &NowPlayingDock::sendBands);
	m_mediaTimer.setInterval(kMediaMs);
	connect(&m_mediaTimer, &QTimer::timeout, this, &NowPlayingDock::pollMedia);

	if (!m_server->listen(m_port))
		obs_log(LOG_WARNING, "[now-playing] port %d is in use, overlay not available", m_port);
}

NowPlayingDock::~NowPlayingDock()
{
	stopCapture();
	m_pool.waitForDone();
}

void NowPlayingDock::loadSettings()
{
	const QString path = QDir(m_dir).filePath(QStringLiteral("settings.json"));
	obs_data_t *data = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
	if (!data)
		return;
	obs_data_set_default_int(data, "port", 8903);
	m_port = static_cast<quint16>(obs_data_get_int(data, "port"));
	m_sourceName = QString::fromUtf8(obs_data_get_string(data, "source"));
	obs_data_set_default_string(data, "color", "#3987E5");
	obs_data_set_default_bool(data, "card", true);
	obs_data_set_default_bool(data, "bars", true);
	m_color = QString::fromUtf8(obs_data_get_string(data, "color"));
	m_card = obs_data_get_bool(data, "card");
	m_bars = obs_data_get_bool(data, "bars");
	m_always = obs_data_get_bool(data, "always");
	obs_data_release(data);
}

void NowPlayingDock::saveSettings()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "port", m_port);
	obs_data_set_string(data, "source", m_sourceName.toUtf8().constData());
	obs_data_set_string(data, "color", m_color.toUtf8().constData());
	obs_data_set_bool(data, "card", m_card);
	obs_data_set_bool(data, "bars", m_bars);
	obs_data_set_bool(data, "always", m_always);
	const QString path = QDir(m_dir).filePath(QStringLiteral("settings.json"));
	obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

QString NowPlayingDock::overlayUrl() const
{
	return QStringLiteral("http://localhost:%1/tocando").arg(m_port);
}

QStringList NowPlayingDock::audioSourceNames()
{
	QStringList names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)
				static_cast<QStringList *>(param)->append(
					QString::fromUtf8(obs_source_get_name(source)));
			return true;
		},
		&names);
	names.sort(Qt::CaseInsensitive);
	return names;
}

void NowPlayingDock::setSource(const QString &name)
{
	if (name == m_sourceName)
		return;
	m_sourceName = name;
	saveSettings();
	if (m_frameTimer.isActive()) {
		stopCapture();
		startCapture();
	}
}

QJsonObject NowPlayingDock::panelState()
{
	QJsonArray sources{QJsonObject{{QStringLiteral("name"), QString()},
				       {QStringLiteral("label"), T("NowPlaying.DesktopAudio")}}};
	QStringList names = audioSourceNames();
	if (!m_sourceName.isEmpty() && !names.contains(m_sourceName))
		names.append(m_sourceName);
	for (const QString &name : names)
		sources.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("label"), name}});
	return QJsonObject{
		{QStringLiteral("url"), overlayUrl()},    {QStringLiteral("listening"), m_server->isListening()},
		{QStringLiteral("source"), m_sourceName}, {QStringLiteral("sources"), sources},
		{QStringLiteral("color"), m_color},       {QStringLiteral("card"), m_card},
		{QStringLiteral("bars"), m_bars},         {QStringLiteral("always"), m_always}};
}

QJsonObject NowPlayingDock::applyPanel(const QJsonObject &panel)
{
	static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString color = panel.value(QStringLiteral("color")).toString();
	if (hex.match(color).hasMatch())
		m_color = color.toUpper();
	m_card = panel.value(QStringLiteral("card")).toBool(m_card);
	m_bars = panel.value(QStringLiteral("bars")).toBool(m_bars);
	m_always = panel.value(QStringLiteral("always")).toBool(m_always);
	saveSettings();
	const QJsonValue source = panel.value(QStringLiteral("source"));
	if (source.isString())
		setSource(source.toString());
	m_server->broadcast(mediaMessage());
	return panelState();
}

void NowPlayingDock::onClients(int count)
{
	if (count > 0 && !m_frameTimer.isActive()) {
		startCapture();
		m_frameTimer.start();
		m_mediaTimer.start();
		pollMedia();
	} else if (count == 0 && m_frameTimer.isActive()) {
		m_frameTimer.stop();
		m_mediaTimer.stop();
		stopCapture();
	}
}

void NowPlayingDock::startCapture()
{
	if (m_source)
		return;
	obs_source_t *source = m_sourceName.isEmpty() ? obs_get_output_source(1) /* Desktop Audio */
						      : obs_get_source_by_name(m_sourceName.toUtf8().constData());
	if (!source)
		return;
	audio_t *audio = obs_get_audio();
	m_channels = audio ? audio_output_get_channels(audio) : 2;
	m_spectrum->clear();
	m_source = obs_source_get_weak_source(source);
	obs_source_add_audio_capture_callback(source, &NowPlayingDock::audioCallback, this);
	obs_source_release(source);
}

void NowPlayingDock::stopCapture()
{
	if (!m_source)
		return;
	obs_source_t *source = obs_weak_source_get_source(m_source);
	if (source) {
		obs_source_remove_audio_capture_callback(source, &NowPlayingDock::audioCallback, this);
		obs_source_release(source);
	}
	obs_weak_source_release(m_source);
	m_source = nullptr;
	m_spectrum->clear();
}

void NowPlayingDock::audioCallback(void *param, obs_source_t *, const struct audio_data *audio, bool muted)
{
	auto *self = static_cast<NowPlayingDock *>(param);
	const size_t channels = std::clamp<size_t>(self->m_channels, 1, MAX_AV_PLANES);
	float mono[256];
	for (uint32_t done = 0; done < audio->frames;) {
		const uint32_t n = std::min<uint32_t>(audio->frames - done, 256);
		for (uint32_t i = 0; i < n; i++) {
			float sum = 0.0f;
			size_t used = 0;
			for (size_t ch = 0; ch < channels; ch++) {
				if (!audio->data[ch])
					continue;
				sum += reinterpret_cast<const float *>(audio->data[ch])[done + i];
				used++;
			}
			/* Muted = not on the stream, so no bars either. */
			mono[i] = muted || !used ? 0.0f : sum / static_cast<float>(used);
		}
		self->m_spectrum->push(mono, n);
		done += n;
	}
}

void NowPlayingDock::sendBands()
{
	if (!m_source)
		startCapture(); /* the source may have been created after the page connected */
	const Spectrum::Frame frame = m_spectrum->analyse();
	QJsonArray bands;
	for (float db : frame.bands)
		bands.append(std::round(std::max(db, -120.0f) * 10.0f) / 10.0);
	m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("bands")},
					{QStringLiteral("bands"), bands},
					{QStringLiteral("rms"), std::round(frame.rmsDb * 10.0f) / 10.0}});
}

void NowPlayingDock::pollMedia()
{
	if (m_mediaBusy)
		return;
	m_mediaBusy = true;
	QPointer<NowPlayingDock> guard(this);
	m_pool.start([guard]() {
		const MediaInfo media = currentMedia();
		QMetaObject::invokeMethod(
			qApp,
			[guard, media]() {
				if (guard)
					guard->onMedia(media);
			},
			Qt::QueuedConnection);
	});
}

void NowPlayingDock::onMedia(const MediaInfo &media)
{
	m_mediaBusy = false;
	m_media = media;
	const QUrl art(media.art);
	m_artFile = art.isLocalFile() ? art.toLocalFile() : QString();
	m_server->broadcast(mediaMessage());
}

QJsonObject NowPlayingDock::mediaMessage() const
{
	QJsonObject media = m_media.toJson();
	if (!media.isEmpty()) {
		const QUrl art(m_media.art);
		if (art.isLocalFile())
			/* The page cannot read file://; the number changes with the file. */
			media.insert(QStringLiteral("art"),
				     QStringLiteral("/art?%1").arg(qHash(m_artFile) ^ qHash(m_media.title)));
		else if (art.scheme() != QLatin1String("http") && art.scheme() != QLatin1String("https"))
			media.insert(QStringLiteral("art"), QString());
	}
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("media")},
			   {QStringLiteral("media"), media.isEmpty() ? QJsonValue() : QJsonValue(media)},
			   {QStringLiteral("lang"), QString::fromLatin1(obs_get_locale())},
			   {QStringLiteral("look"), QJsonObject{{QStringLiteral("color"), m_color},
								{QStringLiteral("card"), m_card},
								{QStringLiteral("bars"), m_bars},
								{QStringLiteral("always"), m_always}}}};
}

bool NowPlayingDock::route(const OverlayServer::Request &request, OverlayServer::Reply &reply)
{
	if (request.method != "GET")
		return false;
	const QString &path = request.path;
	if (path == QLatin1String("/") || path == QLatin1String("/tocando")) {
		reply.file = webFile("now-playing.html");
		return true;
	}
	if (path == QLatin1String("/art")) {
		/* Only the art of the song playing now, never another file. */
		QFile file(m_artFile);
		QByteArray data;
		if (!m_artFile.isEmpty() && QFileInfo(m_artFile).size() <= kMaxArt && file.open(QIODevice::ReadOnly))
			data = file.readAll();
		const QByteArray type = imageType(data);
		if (type.isEmpty()) {
			reply.status = 404;
			reply.type = "text/plain";
			reply.body = "not found";
		} else {
			reply.type = type;
			reply.body = data;
		}
		return true;
	}
	return false;
}

void NowPlayingDock::addBrowserSource()
{
	const QString title = T("NowPlaying.SourceName");
	const QByteArray name = title.toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", overlayUrl().toUtf8().constData());
		obs_data_set_int(settings, "width", 900);
		obs_data_set_int(settings, "height", 170);
		source = obs_source_create("browser_source", name.constData(), settings, nullptr);
		obs_data_release(settings);
	}
	if (!source) {
		QMessageBox::information(static_cast<QWidget *>(obs_frontend_get_main_window()), T("NowPlaying.Title"),
					 T("NowPlaying.NoBrowser").arg(overlayUrl()));
		return;
	}
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene)
		obs_scene_add(scene, source);
	obs_source_release(sceneSource);
	obs_source_release(source);
}

void now_playing_register(void)
{
	/* No panel of its own: a section in the Overlays panel and a tab in the
	 * web panel. Its own server keeps the 30 fps bars off the other
	 * overlays. */
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QPointer<NowPlayingDock> self(new NowPlayingDock(main));
	overlaysAddSection({T("NowPlaying.Title"), T("NowPlaying.Help"),
			    [self]() { return self ? self->overlayUrl() : QString(); },
			    [self]() {
				    if (self)
					    self->addBrowserSource();
			    }});
	overlaysAddPanelTab(QStringLiteral("tocando"), {[self]() { return self ? self->panelState() : QJsonObject(); },
							[self](const QJsonObject &panel) {
								return self ? self->applyPanel(panel) : QJsonObject();
							}});
}
