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

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr const char *kDockId = "meketreve-now-playing";
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

/* Combo that lists the audio sources again every time it opens. */
class SourceCombo : public QComboBox {
public:
	using QComboBox::QComboBox;
	std::function<void()> beforePopup;

protected:
	void showPopup() override
	{
		if (beforePopup)
			beforePopup();
		QComboBox::showPopup();
	}
};

} // namespace

NowPlayingDock::NowPlayingDock(QWidget *parent) : QWidget(parent)
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

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);

	auto *sourceRow = new QHBoxLayout();
	sourceRow->addWidget(new QLabel(T("NowPlaying.AudioSource"), this));
	auto *combo = new SourceCombo(this);
	combo->beforePopup = [this]() {
		refreshSources();
	};
	m_sources = combo;
	m_sources->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_sources->setMinimumContentsLength(12);
	sourceRow->addWidget(m_sources, 1);
	layout->addLayout(sourceRow);
	refreshSources();
	connect(m_sources, &QComboBox::activated, this, [this](int index) {
		const QString name = m_sources->itemData(index).toString();
		if (name == m_sourceName)
			return;
		m_sourceName = name;
		saveSettings();
		if (m_frameTimer.isActive()) {
			stopCapture();
			startCapture();
		}
	});

	auto *buttons = new QHBoxLayout();
	auto *add = new QPushButton(T("NowPlaying.AddSource"), this);
	connect(add, &QPushButton::clicked, this, &NowPlayingDock::addBrowserSource);
	auto *copy = new QPushButton(T("NowPlaying.CopyUrl"), this);
	connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(overlayUrl()); });
	buttons->addWidget(add);
	buttons->addWidget(copy);
	layout->addLayout(buttons);

	auto *hint = new QLabel(T("NowPlaying.Hint"), this);
	hint->setWordWrap(true);
	hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
	layout->addWidget(hint);
	layout->addStretch(1);

	if (!m_server->listen(m_port))
		obs_log(LOG_WARNING, "[now-playing] port %d is in use, overlay not available", m_port);
	refreshStatus();
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
	obs_data_release(data);
}

void NowPlayingDock::saveSettings()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, "port", m_port);
	obs_data_set_string(data, "source", m_sourceName.toUtf8().constData());
	const QString path = QDir(m_dir).filePath(QStringLiteral("settings.json"));
	obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

QString NowPlayingDock::overlayUrl() const
{
	return QStringLiteral("http://localhost:%1/tocando").arg(m_port);
}

void NowPlayingDock::refreshStatus()
{
	if (!m_server->isListening())
		m_status->setText(QStringLiteral("<span style=\"color:#E03C3C\">%1</span>")
					  .arg(T("NowPlaying.PortInUse").arg(m_port).toHtmlEscaped()));
	else
		m_status->setText(
			T("NowPlaying.Status").arg(overlayUrl().toHtmlEscaped()).arg(m_server->clientCount()));
}

void NowPlayingDock::refreshSources()
{
	m_sources->clear();
	m_sources->addItem(T("NowPlaying.DesktopAudio"), QString());
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
	for (const QString &name : names)
		m_sources->addItem(name, name);
	const int index = m_sources->findData(m_sourceName);
	if (index >= 0) {
		m_sources->setCurrentIndex(index);
	} else if (!m_sourceName.isEmpty()) {
		/* Kept even when the source is gone: it may come back with the
		 * scene collection. */
		m_sources->addItem(m_sourceName, m_sourceName);
		m_sources->setCurrentIndex(m_sources->count() - 1);
	}
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
	refreshStatus();
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
			   {QStringLiteral("lang"), QString::fromLatin1(obs_get_locale())}};
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
		QMessageBox::information(this, T("NowPlaying.Title"), T("NowPlaying.NoBrowser").arg(overlayUrl()));
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
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new NowPlayingDock(main);
	auto *scroll = new QScrollArea(main);
	scroll->setWidget(dock);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setMinimumWidth(dock->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("NowPlaying.Title"), scroll)) {
		obs_log(LOG_WARNING, "[now-playing] could not add dock");
		delete scroll;
	}
}
