/*
Meketreve OBS Essentials - Outputs
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

#include "outputs-dock.hpp"
#include "outputs.h"

#include "../config/config-share.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace {

constexpr const char *kDockId = "meketreve-outputs";
constexpr const char *kProfileFile = "meketreve-outputs.json";
constexpr int kStallMs = 5000;

enum SignalKind { kStarted, kStopped, kReconnect, kReconnected };

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString stopReason(int code, obs_output_t *output)
{
	const char *last = output ? obs_output_get_last_error(output) : nullptr;
	if (last && *last)
		return QString::fromUtf8(last);
	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		return T("Outputs.Error.BadPath");
	case OBS_OUTPUT_CONNECT_FAILED:
		return T("Outputs.Error.Connect");
	case OBS_OUTPUT_INVALID_STREAM:
		return T("Outputs.Error.InvalidKey");
	case OBS_OUTPUT_DISCONNECTED:
		return T("Outputs.Error.Disconnected");
	default:
		return T("Outputs.Error.Generic").arg(code);
	}
}

QString formatElapsed(qint64 ms)
{
	const qint64 s = ms / 1000;
	return QStringLiteral("%1:%2:%3")
		.arg(s / 3600, 2, 10, QLatin1Char('0'))
		.arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(s % 60, 2, 10, QLatin1Char('0'));
}

/* Bitrate the main stream's video encoder is set to, or 0 if unknown. */
int mainVideoBitrate()
{
	obs_output_t *main = obs_frontend_get_streaming_output();
	int kbps = 0;
	if (main) {
		obs_encoder_t *venc = obs_output_get_video_encoder(main);
		obs_data_t *s = venc ? obs_encoder_get_settings(venc) : nullptr;
		if (s) {
			kbps = static_cast<int>(obs_data_get_int(s, "bitrate"));
			obs_data_release(s);
		}
		obs_output_release(main);
	}
	if (kbps <= 0) {
		/* Not streaming yet: read the simple-mode setting. */
		config_t *cfg = obs_frontend_get_profile_config();
		if (cfg && config_get_string(cfg, "Output", "Mode") &&
		    strcmp(config_get_string(cfg, "Output", "Mode"), "Simple") == 0)
			kbps = static_cast<int>(config_get_int(cfg, "SimpleOutput", "VBitrate"));
	}
	return kbps;
}

QPointer<OutputsDock> g_dock;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (g_dock)
		g_dock->onFrontendEvent(event);
}

} // namespace

OutputsDock::OutputsDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *bar = new QHBoxLayout();
	auto *startAllButton = new QPushButton(T("Outputs.StartAll"), this);
	connect(startAllButton, &QPushButton::clicked, this, [this]() { startAll(false); });
	auto *stopAllButton = new QPushButton(T("Outputs.StopAll"), this);
	connect(stopAllButton, &QPushButton::clicked, this, [this]() { stopAll(false); });
	auto *add = new QToolButton(this);
	add->setText(QStringLiteral("+"));
	add->setToolTip(T("Outputs.Add"));
	connect(add, &QToolButton::clicked, this, [this]() { editOutput(QString()); });
	bar->addWidget(startAllButton);
	bar->addWidget(stopAllButton);
	bar->addStretch();
	bar->addWidget(add);
	layout->addLayout(bar);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *listWidget = new QWidget(scroll);
	m_list = new QVBoxLayout(listWidget);
	m_list->setContentsMargins(0, 0, 0, 0);
	m_list->setSpacing(2);
	m_empty = new QLabel(T("Outputs.Empty"), listWidget);
	m_empty->setWordWrap(true);
	m_empty->setStyleSheet(QStringLiteral("color: gray"));
	m_list->addWidget(m_empty);
	m_list->addStretch();
	scroll->setWidget(listWidget);
	layout->addWidget(scroll);

	m_timer.setInterval(1000);
	connect(&m_timer, &QTimer::timeout, this, &OutputsDock::tick);
	m_timer.start();
}

OutputsDock::~OutputsDock()
{
	for (auto it = m_running.begin(); it != m_running.end(); ++it)
		releaseRunning(it.value());
	m_running.clear();
}

void OutputsDock::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		loadProfile();
		if (qEnvironmentVariableIsSet("MEKETREVE_SELFTEST_OUTPUTS"))
			runSelfTest();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		loadProfile();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		stopAll(false);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		startAll(true);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		/* Shared encoders go away with the main stream: stop first. */
		stopAll(true);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		for (auto it = m_running.begin(); it != m_running.end(); ++it)
			releaseRunning(it.value());
		m_running.clear();
		break;
	default:
		break;
	}
}

qsizetype OutputsDock::indexOf(const QString &id) const
{
	for (qsizetype i = 0; i < m_outputs.size(); i++) {
		if (m_outputs[i].id == id)
			return i;
	}
	return -1;
}

QStringList OutputsDock::outputIds() const
{
	QStringList ids;
	for (const OutputConfig &c : m_outputs)
		ids.append(c.id);
	return ids;
}

void OutputsDock::loadProfile()
{
	stopAll(false);
	char *dir = obs_frontend_get_current_profile_path();
	m_profilePath = QString::fromUtf8(dir ? dir : "") + QLatin1Char('/') + QLatin1String(kProfileFile);
	bfree(dir);

	m_outputs.clear();
	QFile file(m_profilePath);
	if (file.open(QIODevice::ReadOnly))
		m_outputs = outputsFromJson(
			QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("outputs")).toArray());
	rebuildRows();
	obs_log(LOG_INFO, "[outputs] %d output(s) in this profile", static_cast<int>(m_outputs.size()));
}

void OutputsDock::saveProfile()
{
	if (m_profilePath.isEmpty())
		return;
	/* Stream keys are stored here, in the profile folder, like OBS keeps
	 * its own key in service.json. */
	QSaveFile file(m_profilePath);
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
					     {QStringLiteral("outputs"), outputsToJson(m_outputs, true)}})
			   .toJson(QJsonDocument::Indented));
	if (!file.commit())
		obs_log(LOG_WARNING, "[outputs] could not save %s", m_profilePath.toUtf8().constData());
}

void OutputsDock::rebuildRows()
{
	for (auto it = m_rows.begin(); it != m_rows.end(); ++it)
		delete it.value().dot->parentWidget();
	m_rows.clear();

	QWidget *listWidget = m_empty->parentWidget();
	for (const OutputConfig &c : m_outputs) {
		auto *row = new QWidget(listWidget);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(2, 2, 2, 2);
		Row r;
		r.dot = new QLabel(row);
		auto *name = new QLabel(c.name, row);
		name->setToolTip(QStringLiteral("%1\n%2").arg(c.server, c.sharedEncoder ? T("Outputs.EncoderShared")
											: T("Outputs.EncoderOwn")));
		r.time = new QLabel(row);
		r.time->setStyleSheet(QStringLiteral("color: gray"));
		r.toggle = new QPushButton(row);
		r.toggle->setMinimumWidth(70);
		const QString id = c.id;
		connect(r.toggle, &QPushButton::clicked, this, [this, id]() {
			const auto it = m_running.constFind(id);
			if (it != m_running.constEnd() && it->output)
				stopOutput(id);
			else
				startOutput(id, true);
		});
		auto *more = new QToolButton(row);
		more->setText(QStringLiteral("⋯"));
		more->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new QMenu(more);
		menu->addAction(T("Outputs.Edit"), this, [this, id]() { editOutput(id); });
		menu->addAction(T("Outputs.Remove"), this, [this, id]() { removeOutput(id); });
		more->setMenu(menu);

		h->addWidget(r.dot);
		h->addWidget(name, 1);
		h->addWidget(r.time);
		h->addWidget(r.toggle);
		h->addWidget(more);
		m_list->insertWidget(m_list->count() - 1, row);
		m_rows.insert(id, r);
		refreshRow(id);
	}
	m_empty->setVisible(m_outputs.isEmpty());
}

void OutputsDock::refreshRow(const QString &id)
{
	const auto rowIt = m_rows.constFind(id);
	if (rowIt == m_rows.constEnd())
		return;
	const Row &r = rowIt.value();
	const Running run = m_running.value(id);
	const qsizetype ci = indexOf(id);

	const char *color = "#7A7A7A";
	QString tip = T("Outputs.State.Stopped");
	switch (run.state) {
	case State::Stopped:
		break;
	case State::Starting:
		color = "#E0A000";
		tip = T("Outputs.State.Starting");
		break;
	case State::Live:
		color = "#2EB82E";
		tip = T("Outputs.State.Live");
		break;
	case State::Reconnecting:
		color = "#E0A000";
		tip = T("Outputs.State.Reconnecting");
		break;
	case State::Stopping:
		color = "#E0A000";
		tip = T("Outputs.State.Stopping");
		break;
	case State::Stalled:
	case State::Error:
		color = "#E03C3C";
		tip = run.error;
		break;
	}
	if (ci >= 0 && !m_outputs[ci].enabled)
		tip += QStringLiteral(" (%1)").arg(T("Outputs.Disabled"));
	r.dot->setText(QStringLiteral("<span style=\"color:%1\">&#9679;</span>").arg(QLatin1String(color)));
	r.dot->setToolTip(tip);
	const bool active = run.output != nullptr;
	r.toggle->setText(active ? T("Outputs.Stop") : T("Outputs.Start"));
	r.time->setText(run.state == State::Live || run.state == State::Stalled
				? formatElapsed(QDateTime::currentMSecsSinceEpoch() - run.liveSince)
				: QString());
}

void OutputsDock::tick()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = m_running.begin(); it != m_running.end(); ++it) {
		Running &run = it.value();
		if (!run.output || (run.state != State::Live && run.state != State::Stalled))
			continue;
		/* A live output that stops receiving frames would otherwise look
		 * fine; flag it so the user can restart it or switch encoders. */
		const int frames = obs_output_get_total_frames(run.output);
		if (frames != run.lastFrames) {
			run.lastFrames = frames;
			run.lastProgress = now;
			if (run.state == State::Stalled) {
				run.state = State::Live;
				run.error.clear();
			}
		} else if (run.state == State::Live && now - run.lastProgress > kStallMs) {
			run.state = State::Stalled;
			run.error = T("Outputs.Error.Stalled");
			obs_log(LOG_WARNING, "[outputs] '%s' has sent no video for %d s (%d frames so far)",
				it.key().toUtf8().constData(), kStallMs / 1000, frames);
		}
		refreshRow(it.key());
	}
}

bool OutputsDock::startOutput(const QString &id, bool interactive)
{
	const qsizetype ci = indexOf(id);
	if (ci < 0)
		return false;
	const OutputConfig &c = m_outputs[ci];
	Running &run = m_running[id];
	if (run.output)
		return true;

	const auto failWith = [&](const QString &why) {
		run.state = State::Error;
		run.error = why;
		obs_log(LOG_WARNING, "[outputs] '%s' not started: %s", c.name.toUtf8().constData(),
			why.toUtf8().constData());
		refreshRow(id);
		if (interactive)
			QMessageBox::warning(this, c.name, why);
		return false;
	};

	if (c.server.isEmpty())
		return failWith(T("Outputs.Error.NoServer"));
	if (c.key.isEmpty() && !c.isSrt())
		return failWith(T("Outputs.Error.NoKey"));

	obs_encoder_t *venc = nullptr;
	obs_encoder_t *aenc = nullptr;
	if (c.sharedEncoder) {
		obs_output_t *main = obs_frontend_get_streaming_output();
		const bool mainLive = main && obs_output_active(main);
		if (mainLive) {
			venc = obs_output_get_video_encoder(main);
			aenc = obs_output_get_audio_encoder(main, 0);
		}
		obs_output_release(main);
		if (!mainLive || !venc || !aenc)
			return failWith(T("Outputs.Error.MainNotLive"));
	} else {
		video_t *video = obs_get_video();
		if (!c.canvas.isEmpty()) {
			obs_canvas_t *canvas = obs_get_canvas_by_name(c.canvas.toUtf8().constData());
			video = canvas ? obs_canvas_get_video(canvas) : nullptr;
			obs_canvas_release(canvas);
			if (!video)
				return failWith(T("Outputs.Error.NoCanvas").arg(c.canvas));
		}
		obs_data_t *vs = obs_data_create();
		obs_data_set_string(vs, "rate_control", "CBR");
		obs_data_set_int(vs, "bitrate", c.videoBitrate);
		obs_data_set_int(vs, "keyint_sec", 2);
		run.ownVideo = obs_video_encoder_create(c.videoEncoder.toUtf8().constData(),
							("meketreve-out-video-" + id).toUtf8().constData(), vs,
							nullptr);
		obs_data_release(vs);
		obs_data_t *as = obs_data_create();
		obs_data_set_int(as, "bitrate", c.audioBitrate);
		run.ownAudio = obs_audio_encoder_create(
			"ffmpeg_aac", ("meketreve-out-audio-" + id).toUtf8().constData(), as, 0, nullptr);
		obs_data_release(as);
		if (!run.ownVideo || !run.ownAudio) {
			releaseRunning(run);
			return failWith(T("Outputs.Error.Encoder").arg(c.videoEncoder));
		}
		obs_encoder_set_video(run.ownVideo, video);
		obs_encoder_set_audio(run.ownAudio, obs_get_audio());
		venc = run.ownVideo;
		aenc = run.ownAudio;
	}

	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", c.server.toUtf8().constData());
	obs_data_set_string(ss, "key", c.key.toUtf8().constData());
	run.service =
		obs_service_create("rtmp_custom", ("meketreve-out-service-" + id).toUtf8().constData(), ss, nullptr);
	obs_data_release(ss);

	run.output = obs_output_create(c.outputType().toUtf8().constData(),
				       ("meketreve-out-" + id).toUtf8().constData(), nullptr, nullptr);
	if (!run.service || !run.output) {
		releaseRunning(run);
		return failWith(T("Outputs.Error.Generic").arg(-1));
	}
	obs_output_set_service(run.output, run.service);

	/* Same network options (bind IP, delay) as the main stream. */
	config_t *cfg = obs_frontend_get_profile_config();
	if (cfg) {
		obs_data_t *os = obs_data_create();
		const char *bindIp = config_get_string(cfg, "Output", "BindIP");
		obs_data_set_string(os, "bind_ip", bindIp ? bindIp : "default");
		obs_output_update(run.output, os);
		obs_data_release(os);
		if (config_get_bool(cfg, "Output", "DelayEnable"))
			obs_output_set_delay(
				run.output, static_cast<uint32_t>(config_get_int(cfg, "Output", "DelaySec")),
				config_get_bool(cfg, "Output", "DelayPreserve") ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	}

	signal_handler_t *sh = obs_output_get_signal_handler(run.output);
	signal_handler_connect(sh, "start", outputStarted, this);
	signal_handler_connect(sh, "stop", outputStopped, this);
	signal_handler_connect(sh, "reconnect", outputReconnect, this);
	signal_handler_connect(sh, "reconnect_success", outputReconnected, this);

	obs_output_set_video_encoder(run.output, venc);
	obs_output_set_audio_encoder(run.output, aenc, 0);

	run.state = State::Starting;
	run.error.clear();
	refreshRow(id);
	if (!obs_output_start(run.output)) {
		const QString why = stopReason(OBS_OUTPUT_ERROR, run.output);
		releaseRunning(run);
		return failWith(why);
	}
	obs_log(LOG_INFO, "[outputs] starting '%s' (%s encoder)", c.name.toUtf8().constData(),
		c.sharedEncoder ? "shared" : c.videoEncoder.toUtf8().constData());
	return true;
}

void OutputsDock::stopOutput(const QString &id)
{
	auto it = m_running.find(id);
	if (it == m_running.end() || !it->output)
		return;
	it->state = State::Stopping;
	refreshRow(id);
	/* The "stop" signal releases everything once the output is down. */
	obs_output_stop(it->output);
}

void OutputsDock::releaseRunning(Running &r)
{
	if (r.output) {
		signal_handler_t *sh = obs_output_get_signal_handler(r.output);
		signal_handler_disconnect(sh, "start", outputStarted, this);
		signal_handler_disconnect(sh, "stop", outputStopped, this);
		signal_handler_disconnect(sh, "reconnect", outputReconnect, this);
		signal_handler_disconnect(sh, "reconnect_success", outputReconnected, this);
		if (obs_output_active(r.output))
			obs_output_force_stop(r.output);
		obs_output_release(r.output);
	}
	obs_service_release(r.service);
	obs_encoder_release(r.ownVideo);
	obs_encoder_release(r.ownAudio);
	r.output = nullptr;
	r.service = nullptr;
	r.ownVideo = nullptr;
	r.ownAudio = nullptr;
}

void OutputsDock::startAll(bool followersOnly)
{
	for (const OutputConfig &c : m_outputs) {
		if (c.enabled && (!followersOnly || c.followMain))
			startOutput(c.id, false);
	}
}

void OutputsDock::stopAll(bool followersOnly)
{
	for (const OutputConfig &c : m_outputs) {
		if (!followersOnly || c.followMain || c.sharedEncoder)
			stopOutput(c.id);
	}
}

QString OutputsDock::stateText(const QString &id) const
{
	const Running run = m_running.value(id);
	switch (run.state) {
	case State::Stopped:
		return QStringLiteral("stopped");
	case State::Starting:
		return QStringLiteral("starting");
	case State::Live:
		return QStringLiteral("live %1s").arg((QDateTime::currentMSecsSinceEpoch() - run.liveSince) / 1000);
	case State::Reconnecting:
		return QStringLiteral("reconnecting");
	case State::Stopping:
		return QStringLiteral("stopping");
	case State::Stalled:
		return QStringLiteral("stalled after %1 frames").arg(obs_output_get_total_frames(run.output));
	case State::Error:
		return QStringLiteral("error: ") + run.error;
	}
	return QString();
}

void OutputsDock::onOutputSignal(obs_output_t *output, int kind, int code)
{
	for (auto it = m_running.begin(); it != m_running.end(); ++it) {
		Running &run = it.value();
		if (run.output != output)
			continue;
		const QString id = it.key();
		const qsizetype ci = indexOf(id);
		const QByteArray name = ci >= 0 ? m_outputs[ci].name.toUtf8() : id.toUtf8();
		switch (kind) {
		case kStarted:
			run.state = State::Live;
			run.liveSince = QDateTime::currentMSecsSinceEpoch();
			run.lastProgress = run.liveSince;
			run.lastFrames = -1;
			obs_log(LOG_INFO, "[outputs] '%s' is live", name.constData());
			break;
		case kReconnect:
			run.state = State::Reconnecting;
			break;
		case kReconnected:
			run.state = State::Live;
			break;
		case kStopped:
			if (code == OBS_OUTPUT_SUCCESS) {
				run.state = State::Stopped;
				obs_log(LOG_INFO, "[outputs] '%s' stopped", name.constData());
			} else {
				run.state = State::Error;
				run.error = stopReason(code, output);
				obs_log(LOG_WARNING, "[outputs] '%s' stopped: %s", name.constData(),
					run.error.toUtf8().constData());
			}
			releaseRunning(run);
			break;
		default:
			break;
		}
		refreshRow(id);
		return;
	}
}

void OutputsDock::outputStarted(void *data, calldata_t *cd)
{
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QMetaObject::invokeMethod(
		static_cast<OutputsDock *>(data),
		[dock = static_cast<OutputsDock *>(data), output]() { dock->onOutputSignal(output, kStarted, 0); },
		Qt::QueuedConnection);
}

void OutputsDock::outputStopped(void *data, calldata_t *cd)
{
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	const int code = static_cast<int>(calldata_int(cd, "code"));
	QMetaObject::invokeMethod(
		static_cast<OutputsDock *>(data),
		[dock = static_cast<OutputsDock *>(data), output, code]() {
			dock->onOutputSignal(output, kStopped, code);
		},
		Qt::QueuedConnection);
}

void OutputsDock::outputReconnect(void *data, calldata_t *cd)
{
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QMetaObject::invokeMethod(
		static_cast<OutputsDock *>(data),
		[dock = static_cast<OutputsDock *>(data), output]() { dock->onOutputSignal(output, kReconnect, 0); },
		Qt::QueuedConnection);
}

void OutputsDock::outputReconnected(void *data, calldata_t *cd)
{
	auto *output = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QMetaObject::invokeMethod(
		static_cast<OutputsDock *>(data),
		[dock = static_cast<OutputsDock *>(data), output]() { dock->onOutputSignal(output, kReconnected, 0); },
		Qt::QueuedConnection);
}

/* Developer smoke test, inert unless MEKETREVE_SELFTEST_OUTPUTS is set:
 * starts the main stream (outputs that follow it start too), then logs
 * every output's state and stops. */
void OutputsDock::runSelfTest()
{
	const auto report = [this](const char *when) {
		for (const OutputConfig &c : m_outputs)
			obs_log(LOG_INFO, "[selftest] outputs %s: %s = %s", when, c.name.toUtf8().constData(),
				stateText(c.id).toUtf8().constData());
	};
	QTimer::singleShot(2000, this, [this, report]() {
		obs_log(LOG_INFO, "[selftest] outputs: starting main stream");
		report("before");
		obs_frontend_streaming_start();
		QTimer::singleShot(4000, this, [this, report]() {
			report("after 4s");
			startAll(false);
			QTimer::singleShot(8000, this, [report]() {
				report("after 12s");
				obs_frontend_streaming_stop();
				QTimer::singleShot(3000, [report]() {
					report("after stop");
					obs_log(LOG_INFO, "[selftest] outputs done");
				});
			});
		});
	});
}

void OutputsDock::editOutput(const QString &id)
{
	const qsizetype existing = indexOf(id);
	OutputConfig c = existing >= 0 ? m_outputs[existing] : OutputConfig();

	QDialog dialog(this);
	dialog.setWindowTitle(existing >= 0 ? T("Outputs.Edit") : T("Outputs.Add"));
	dialog.setMinimumWidth(460);
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout();

	auto *platform = new QComboBox(&dialog);
	for (const PlatformDefaults &p : outputPlatforms())
		platform->addItem(QString::fromLatin1(p.label), QString::fromLatin1(p.id));
	platform->setCurrentIndex(std::max(0, platform->findData(c.platform)));
	form->addRow(T("Outputs.Platform"), platform);

	auto *name = new QLineEdit(c.name, &dialog);
	form->addRow(T("Outputs.Name"), name);
	auto *server = new QLineEdit(c.server, &dialog);
	form->addRow(T("Outputs.Server"), server);

	auto *keyRow = new QHBoxLayout();
	auto *key = new QLineEdit(c.key, &dialog);
	key->setEchoMode(QLineEdit::Password);
	auto *showKey = new QCheckBox(T("Outputs.ShowKey"), &dialog);
	connect(showKey, &QCheckBox::toggled, key,
		[key](bool on) { key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	keyRow->addWidget(key, 1);
	keyRow->addWidget(showKey);
	form->addRow(T("Outputs.Key"), keyRow);

	/* Extra canvases (the vertical one, for example) always use their own
	 * encoder: the main stream's encoder only sees the main canvas. */
	auto *canvas = new QComboBox(&dialog);
	canvas->addItem(T("Outputs.MainCanvas"), QString());
	obs_enum_canvases(
		[](void *param, obs_canvas_t *cv) {
			auto *combo = static_cast<QComboBox *>(param);
			const QString canvasName = QString::fromUtf8(obs_canvas_get_name(cv));
			if (!canvasName.isEmpty() && (obs_canvas_get_flags(cv) & MAIN) == 0 && obs_canvas_has_video(cv))
				combo->addItem(canvasName, canvasName);
			return true;
		},
		canvas);
	if (!c.canvas.isEmpty() && canvas->findData(c.canvas) < 0)
		canvas->addItem(c.canvas, c.canvas);
	canvas->setCurrentIndex(std::max(0, canvas->findData(c.canvas)));
	form->addRow(T("Outputs.Canvas"), canvas);

	auto *encoderMode = new QComboBox(&dialog);
	encoderMode->addItem(T("Outputs.EncoderShared"), true);
	encoderMode->addItem(T("Outputs.EncoderOwn"), false);
	encoderMode->setCurrentIndex(c.sharedEncoder ? 0 : 1);
	form->addRow(T("Outputs.Encoder"), encoderMode);

	auto *videoEncoder = new QComboBox(&dialog);
	const char *encId = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &encId); i++) {
		if (obs_get_encoder_type(encId) != OBS_ENCODER_VIDEO)
			continue;
		if (obs_get_encoder_caps(encId) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		videoEncoder->addItem(QString::fromUtf8(obs_encoder_get_display_name(encId)), QString::fromUtf8(encId));
	}
	videoEncoder->setCurrentIndex(std::max(0, videoEncoder->findData(c.videoEncoder)));
	form->addRow(T("Outputs.VideoEncoder"), videoEncoder);

	auto *videoBitrate = new QSpinBox(&dialog);
	videoBitrate->setRange(200, 100000);
	videoBitrate->setSingleStep(500);
	videoBitrate->setSuffix(QStringLiteral(" kbps"));
	videoBitrate->setValue(c.videoBitrate);
	form->addRow(T("Outputs.VideoBitrate"), videoBitrate);
	auto *audioBitrate = new QSpinBox(&dialog);
	audioBitrate->setRange(32, 512);
	audioBitrate->setSingleStep(32);
	audioBitrate->setSuffix(QStringLiteral(" kbps"));
	audioBitrate->setValue(c.audioBitrate);
	form->addRow(T("Outputs.AudioBitrate"), audioBitrate);

	auto *follow = new QCheckBox(T("Outputs.FollowMain"), &dialog);
	follow->setChecked(c.followMain);
	form->addRow(QString(), follow);
	auto *enabled = new QCheckBox(T("Outputs.Enabled"), &dialog);
	enabled->setChecked(c.enabled);
	form->addRow(QString(), enabled);
	layout->addLayout(form);

	auto *warning = new QLabel(&dialog);
	warning->setWordWrap(true);
	warning->setStyleSheet(QStringLiteral("color: #E0A000"));
	layout->addWidget(warning);
	auto *note = new QLabel(T("Outputs.KeyNote"), &dialog);
	note->setWordWrap(true);
	note->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(note);

	const auto update = [=]() {
		const bool extraCanvas = !canvas->currentData().toString().isEmpty();
		if (extraCanvas)
			encoderMode->setCurrentIndex(1);
		encoderMode->setEnabled(!extraCanvas);
		const bool own = !encoderMode->currentData().toBool();
		videoEncoder->setEnabled(own);
		videoBitrate->setEnabled(own);
		audioBitrate->setEnabled(own);
		const PlatformDefaults *p = findPlatform(platform->currentData().toString());
		const int kbps = own ? videoBitrate->value() : mainVideoBitrate();
		QString text;
		if (p && p->maxVideoKbps > 0 && kbps > p->maxVideoKbps)
			text = T("Outputs.BitrateWarning")
				       .arg(QString::fromLatin1(p->label))
				       .arg(p->maxVideoKbps)
				       .arg(kbps);
		if (!own)
			text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + T("Outputs.SharedNote");
		warning->setText(text);
	};
	connect(platform, &QComboBox::currentIndexChanged, &dialog, [=]() {
		const PlatformDefaults *p = findPlatform(platform->currentData().toString());
		if (!p)
			return;
		const bool nameIsDefault = name->text().isEmpty() ||
					   std::any_of(outputPlatforms().begin(), outputPlatforms().end(),
						       [&name](const PlatformDefaults &d) {
							       return name->text() == QLatin1String(d.label);
						       });
		if (nameIsDefault)
			name->setText(QString::fromLatin1(p->label));
		if (server->text().isEmpty() || std::any_of(outputPlatforms().begin(), outputPlatforms().end(),
							    [&server](const PlatformDefaults &d) {
								    return server->text() == QLatin1String(d.server);
							    }))
			server->setText(QString::fromLatin1(p->server));
		update();
	});
	connect(encoderMode, &QComboBox::currentIndexChanged, &dialog, update);
	connect(canvas, &QComboBox::currentIndexChanged, &dialog, update);
	connect(videoBitrate, &QSpinBox::valueChanged, &dialog, update);
	if (existing < 0 && c.server.isEmpty())
		Q_EMIT platform->currentIndexChanged(platform->currentIndex());
	update();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return;

	c.platform = platform->currentData().toString();
	c.name = name->text().trimmed().isEmpty() ? platform->currentText() : name->text().trimmed();
	c.server = server->text().trimmed();
	c.key = key->text().trimmed();
	c.canvas = canvas->currentData().toString();
	c.sharedEncoder = c.canvas.isEmpty() && encoderMode->currentData().toBool();
	c.videoEncoder = videoEncoder->currentData().toString();
	c.videoBitrate = videoBitrate->value();
	c.audioBitrate = audioBitrate->value();
	c.followMain = follow->isChecked();
	c.enabled = enabled->isChecked();

	if (existing >= 0) {
		m_outputs[existing] = c;
	} else {
		c.id = newOutputId();
		m_outputs.append(c);
	}
	saveProfile();
	rebuildRows();
}

void OutputsDock::removeOutput(const QString &id)
{
	const qsizetype ci = indexOf(id);
	if (ci < 0)
		return;
	if (QMessageBox::question(this, T("Outputs.Remove"), T("Outputs.RemoveConfirm").arg(m_outputs[ci].name)) !=
	    QMessageBox::Yes)
		return;
	auto it = m_running.find(id);
	if (it != m_running.end()) {
		releaseRunning(it.value());
		m_running.erase(it);
	}
	m_outputs.removeAt(ci);
	saveProfile();
	rebuildRows();
}

QJsonArray OutputsDock::exportOutputs() const
{
	return outputsToJson(m_outputs, false);
}

void OutputsDock::importOutputs(const QJsonArray &arr)
{
	int added = 0;
	for (OutputConfig c : outputsFromJson(arr)) {
		/* An output to the same server is already here, key included. */
		const bool exists = std::any_of(m_outputs.begin(), m_outputs.end(), [&c](const OutputConfig &o) {
			return o.server == c.server && o.platform == c.platform;
		});
		if (exists)
			continue;
		c.id = newOutputId();
		c.key.clear();
		m_outputs.append(c);
		added++;
	}
	saveProfile();
	rebuildRows();
	obs_log(LOG_INFO, "[outputs] imported %d output(s)", added);
}

QString OutputsDock::describeOutputs(const QJsonArray &arr)
{
	QStringList names;
	for (const OutputConfig &c : outputsFromJson(arr))
		names.append(c.name);
	return names.isEmpty() ? T("Outputs.None") : names.join(QStringLiteral(", "));
}

void outputs_register(void)
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new OutputsDock(main);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Outputs.Title"), dock)) {
		obs_log(LOG_WARNING, "[outputs] could not add dock");
		delete dock;
		return;
	}
	g_dock = dock;
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	configShareAddSection({QStringLiteral("outputs"), "Config.Section.Outputs",
			       []() { return g_dock ? QJsonValue(g_dock->exportOutputs()) : QJsonValue(); },
			       [](const QJsonValue &v) {
				       if (g_dock)
					       g_dock->importOutputs(v.toArray());
			       },
			       [](const QJsonValue &v) {
				       return OutputsDock::describeOutputs(v.toArray());
			       }});
}

void outputs_unregister(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
}
