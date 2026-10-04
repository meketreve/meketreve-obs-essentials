/*
Meketreve OBS Essentials - Alerts
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
#include "alerts-dock.hpp"
#include "alerts.h"
#include "chat-overlay.hpp"

#include "../config/config-share.hpp"
#include "../texuguito/tts-client.hpp"
#include "../unified-chat/unified-chat-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr const char *kDockId = "meketreve-alerts";
constexpr qsizetype kMaxUpload = 30 * 1024 * 1024;
constexpr int kTtsKeep = 20;
constexpr int kTtsMaxChars = 200;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString moduleConfigDir()
{
	char *dir = obs_module_config_path("alerts");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	QDir().mkpath(path);
	return path;
}

QString webDir()
{
	char *dir = obs_module_file("alerts/web");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	return path;
}

QString language()
{
	const char *locale = obs_get_locale();
	return locale && QByteArray(locale).startsWith("pt") ? QStringLiteral("pt") : QStringLiteral("en");
}

QByteArray json(const QJsonValue &value)
{
	return value.isObject() ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
				: QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
}

void jsonReply(OverlayServer::Reply &reply, int status, const QJsonObject &body)
{
	reply.status = status;
	reply.type = "application/json";
	reply.body = json(body);
}

void errorReply(OverlayServer::Reply &reply, int status, const QString &error)
{
	jsonReply(reply, status, QJsonObject{{QStringLiteral("error"), error}});
}

QPointer<AlertsDock> g_dock;
QHash<QString, PanelTab> g_panelTabs;
QList<OverlaySection> g_pendingSections;

} // namespace

void overlaysAddPanelTab(const QString &name, PanelTab tab)
{
	g_panelTabs.insert(name, std::move(tab));
}

void overlaysAddSection(OverlaySection section)
{
	if (g_dock)
		g_dock->addSection(section);
	else
		g_pendingSections.append(std::move(section));
}

void AlertsDock::addSection(const OverlaySection &section)
{
	auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(section.title.toHtmlEscaped()), this);
	m_extraSections->addSpacing(6);
	m_extraSections->addWidget(title);
	auto *row = new QHBoxLayout();
	auto *add = new QPushButton(T("Alerts.AddSource"), this);
	connect(add, &QPushButton::clicked, this, [section]() { section.addToScene(); });
	auto *copy = new QPushButton(T("Alerts.CopyUrl"), this);
	connect(copy, &QPushButton::clicked, this, [section]() { QApplication::clipboard()->setText(section.url()); });
	row->addWidget(add);
	row->addWidget(copy);
	m_extraSections->addLayout(row);
	auto *help = new QLabel(section.help, this);
	help->setWordWrap(true);
	help->setStyleSheet(QStringLiteral("color: gray"));
	m_extraSections->addWidget(help);
}

AlertsDock::AlertsDock(UnifiedChatDock *chat, QWidget *parent) : QWidget(parent), m_chat(chat)
{
	m_dir = moduleConfigDir();
	m_mediaDir = QDir(m_dir).filePath(QStringLiteral("media"));
	QDir().mkpath(m_mediaDir);
	loadSettings();
	loadConfig();
	loadWidgets();

	OverlayServer::Routes routes;
	routes.webDir = webDir();
	routes.ttsClip = [this](const QString &id) {
		return m_tts.value(id);
	};
	routes.snapshot = [this]() {
		/* Alert pages read "config", chat pages "chat". */
		QJsonObject snapshot{{QStringLiteral("type"), QStringLiteral("config")},
				     {QStringLiteral("config"), overlayConfig()},
				     {QStringLiteral("chat"), m_chatConfig},
				     {QStringLiteral("events"), historyMessage(QStringLiteral("events-history"))}};
		widgetsSnapshot(snapshot);
		return snapshot;
	};
	routes.handler = [this](const OverlayServer::Request &request, OverlayServer::Reply &reply) {
		return route(request, reply);
	};
	routes.maxBody = kMaxUpload;
	m_server = new OverlayServer(routes, this);
	connect(m_server, &OverlayServer::clientsChanged, this, &AlertsDock::refreshStatus);
	connect(m_chat, &UnifiedChatDock::incoming, this, &AlertsDock::onChat);
	connect(m_chat, &UnifiedChatDock::shown, this, &AlertsDock::onShown);
	connect(m_chat, &UnifiedChatDock::activity, this, &AlertsDock::onActivity);
	obs_frontend_add_event_callback(frontendEvent, this);
	connect(m_chat, &UnifiedChatDock::removed, this, &AlertsDock::onRemoved);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);

	/* One web panel sets up every overlay; each overlay below only has its
	 * own "Add to scene" and link. */
	auto *top = new QHBoxLayout();
	auto *panel = new QPushButton(T("Overlays.OpenPanel"), this);
	connect(panel, &QPushButton::clicked, this, [this]() {
		if (!m_server->isListening()) {
			refreshStatus();
			return;
		}
		QDesktopServices::openUrl(QUrl(panelUrl(QStringLiteral("alertas"))));
	});
	m_toggle = new QPushButton(this);
	connect(m_toggle, &QPushButton::clicked, this, [this]() {
		m_enabled = !m_enabled;
		saveSettings();
		applyEnabled();
	});
	auto *settings = new QToolButton(this);
	settings->setText(T("UnifiedChat.Settings"));
	connect(settings, &QToolButton::clicked, this, &AlertsDock::openSettings);
	top->addWidget(panel, 1);
	top->addWidget(m_toggle);
	top->addWidget(settings);
	layout->addLayout(top);

	const auto section = [this, layout](const char *titleKey, const char *helpKey, QHBoxLayout *buttons) {
		auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(T(titleKey).toHtmlEscaped()), this);
		layout->addSpacing(6);
		layout->addWidget(title);
		layout->addLayout(buttons);
		auto *help = new QLabel(T(helpKey), this);
		help->setWordWrap(true);
		help->setStyleSheet(QStringLiteral("color: gray"));
		layout->addWidget(help);
	};
	const auto sourceButtons = [this](QHBoxLayout *row, std::function<void()> add, std::function<QString()> url) {
		auto *addButton = new QPushButton(T("Alerts.AddSource"), this);
		connect(addButton, &QPushButton::clicked, this, add);
		auto *copy = new QPushButton(T("Alerts.CopyUrl"), this);
		connect(copy, &QPushButton::clicked, this, [url]() { QApplication::clipboard()->setText(url()); });
		row->addWidget(addButton);
		row->addWidget(copy);
	};

	auto *alertsRow = new QHBoxLayout();
	auto *test = new QToolButton(this);
	test->setText(T("Alerts.Test"));
	test->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(test);
	for (const QString &type : Alerts::types()) {
		const QByteArray key = "Alerts.Type." + type.toLatin1();
		menu->addAction(T(key.constData()), this, [this, type]() { fire(Alerts::sample(type, T)); });
	}
	test->setMenu(menu);
	auto *skip = new QToolButton(this);
	skip->setText(T("Alerts.Skip"));
	connect(skip, &QToolButton::clicked, this,
		[this]() { m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("skip")}}); });
	sourceButtons(
		alertsRow,
		[this]() {
			obs_video_info ovi{};
			obs_get_video_info(&ovi);
			addBrowserSource(T("Alerts.SourceName"), overlayUrl(), static_cast<int>(ovi.base_width),
					 static_cast<int>(ovi.base_height), true);
		},
		[this]() { return overlayUrl(); });
	alertsRow->addWidget(test);
	alertsRow->addWidget(skip);
	section("Alerts.Title", "Alerts.Help", alertsRow);

	auto *chatRow = new QHBoxLayout();
	sourceButtons(
		chatRow, [this]() { addBrowserSource(T("ChatOverlay.SourceName"), chatOverlayUrl(), 480, 720, false); },
		[this]() { return chatOverlayUrl(); });
	section("ChatOverlay.Title", "ChatOverlay.Help", chatRow);

	auto *eventsRow = new QHBoxLayout();
	sourceButtons(
		eventsRow,
		[this]() { addBrowserSource(T("EventsOverlay.SourceName"), eventsOverlayUrl(), 420, 400, false); },
		[this]() { return eventsOverlayUrl(); });
	section("EventsOverlay.Title", "EventsOverlay.Help", eventsRow);

	auto *goalsRow = new QHBoxLayout();
	sourceButtons(
		goalsRow, [this]() { addBrowserSource(T("Goals.SourceName"), goalsOverlayUrl(), 600, 300, false); },
		[this]() { return goalsOverlayUrl(); });
	section("Goals.Title", "Goals.Help", goalsRow);

	auto *pollRow = new QHBoxLayout();
	sourceButtons(
		pollRow, [this]() { addBrowserSource(T("Poll.SourceName"), pollOverlayUrl(), 600, 400, false); },
		[this]() { return pollOverlayUrl(); });
	section("Poll.Title", "Poll.Help", pollRow);

	auto *subathonRow = new QHBoxLayout();
	sourceButtons(
		subathonRow,
		[this]() { addBrowserSource(T("Subathon.SourceName"), subathonOverlayUrl(), 600, 200, false); },
		[this]() { return subathonOverlayUrl(); });
	section("Subathon.Title", "Subathon.Help", subathonRow);

	/* Overlays with their own server (the chat parade, now playing). */
	m_extraSections = new QVBoxLayout();
	m_extraSections->setContentsMargins(0, 0, 0, 0);
	layout->addLayout(m_extraSections);
	for (const OverlaySection &pending : std::as_const(g_pendingSections))
		addSection(pending);
	g_pendingSections.clear();
	layout->addStretch();

	applyEnabled();
}

AlertsDock::~AlertsDock()
{
	obs_frontend_remove_event_callback(frontendEvent, this);
	m_server->close();
}

void AlertsDock::frontendEvent(enum obs_frontend_event event, void *data)
{
	auto *self = static_cast<AlertsDock *>(data);
	if (event != OBS_FRONTEND_EVENT_STREAMING_STARTED)
		return;
	/* A new live: "top donor" and "top bits" count from zero. */
	if (self->m_eventsConfig.value(QStringLiteral("resetTopOnLive")).toBool()) {
		self->m_history.resetTop();
		self->saveHistory();
		self->broadcastHistory();
	}
	self->widgetsLiveStarted();
}

QString AlertsDock::overlayUrl() const
{
	return QStringLiteral("http://localhost:%1/alertas").arg(m_port);
}

QString AlertsDock::chatOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/chat").arg(m_port);
}

QString AlertsDock::panelUrl(const QString &tab) const
{
	/* The token goes after "#": browsers never send that part anywhere. */
	return QStringLiteral("http://localhost:%1/painel#t=%2&aba=%3").arg(m_port).arg(m_token, tab);
}

QString AlertsDock::eventsOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/eventos").arg(m_port);
}

QString AlertsDock::goalsOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/metas").arg(m_port);
}

QString AlertsDock::pollOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/enquete").arg(m_port);
}

QString AlertsDock::subathonOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/subathon").arg(m_port);
}

void AlertsDock::loadSettings()
{
	const QString path = QDir(m_dir).filePath(QStringLiteral("settings.json"));
	obs_data_t *data = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
	if (data) {
		obs_data_set_default_bool(data, "enabled", true);
		obs_data_set_default_int(data, "port", 8902);
		m_enabled = obs_data_get_bool(data, "enabled");
		m_port = static_cast<quint16>(obs_data_get_int(data, "port"));
		m_token = QString::fromUtf8(obs_data_get_string(data, "token"));
		obs_data_release(data);
	}
	if (m_token.size() < 32) {
		QByteArray random(16, Qt::Uninitialized);
		QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(random.data()), 4);
		m_token = QString::fromLatin1(random.toHex());
		saveSettings();
	}
}

void AlertsDock::saveSettings()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", m_enabled);
	obs_data_set_int(data, "port", m_port);
	obs_data_set_string(data, "token", m_token.toUtf8().constData());
	const QString path = QDir(m_dir).filePath(QStringLiteral("settings.json"));
	obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

void AlertsDock::loadConfig()
{
	QFile file(QDir(m_dir).filePath(QStringLiteral("alerts.json")));
	QJsonObject stored;
	if (file.open(QIODevice::ReadOnly))
		stored = QJsonDocument::fromJson(file.readAll()).object();
	m_config = stored.isEmpty() ? Alerts::defaults(T) : Alerts::normalize(stored, T);

	QFile chatFile(QDir(m_dir).filePath(QStringLiteral("chat-overlay.json")));
	QJsonObject chatStored;
	if (chatFile.open(QIODevice::ReadOnly))
		chatStored = QJsonDocument::fromJson(chatFile.readAll()).object();
	m_chatConfig = ChatOverlay::normalize(chatStored);

	QFile eventsFile(QDir(m_dir).filePath(QStringLiteral("events-overlay.json")));
	QJsonObject eventsStored;
	if (eventsFile.open(QIODevice::ReadOnly))
		eventsStored = QJsonDocument::fromJson(eventsFile.readAll()).object();
	m_eventsConfig = EventsOverlay::normalize(eventsStored, T);

	QFile historyFile(QDir(m_dir).filePath(QStringLiteral("event-history.json")));
	if (historyFile.open(QIODevice::ReadOnly))
		m_history.load(QJsonDocument::fromJson(historyFile.readAll()).object());
}

void AlertsDock::saveHistory()
{
	QSaveFile file(QDir(m_dir).filePath(QStringLiteral("event-history.json")));
	if (!file.open(QIODevice::WriteOnly) ||
	    file.write(QJsonDocument(m_history.save()).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
		obs_log(LOG_WARNING, "[alerts] could not save event-history.json");
}

QJsonObject AlertsDock::historyMessage(const QString &type) const
{
	return QJsonObject{{QStringLiteral("type"), type},
			   {QStringLiteral("config"), m_eventsConfig},
			   {QStringLiteral("recent"), m_history.recent(20)},
			   {QStringLiteral("labels"), m_history.labels()}};
}

void AlertsDock::broadcastHistory()
{
	m_server->broadcast(historyMessage(QStringLiteral("events-history")));
}

void AlertsDock::importEventsConfig(const QJsonObject &config)
{
	m_eventsConfig = EventsOverlay::normalize(config, T);
	QSaveFile file(QDir(m_dir).filePath(QStringLiteral("events-overlay.json")));
	if (!file.open(QIODevice::WriteOnly) ||
	    file.write(QJsonDocument(m_eventsConfig).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
		obs_log(LOG_WARNING, "[alerts] could not save events-overlay.json");
	broadcastHistory();
}

void AlertsDock::saveChatConfig()
{
	QSaveFile file(QDir(m_dir).filePath(QStringLiteral("chat-overlay.json")));
	if (!file.open(QIODevice::WriteOnly) ||
	    file.write(QJsonDocument(m_chatConfig).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
		obs_log(LOG_WARNING, "[alerts] could not save chat-overlay.json");
}

void AlertsDock::importChatConfig(const QJsonObject &config)
{
	m_chatConfig = ChatOverlay::normalize(config);
	saveChatConfig();
	m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat-config")},
					{QStringLiteral("config"), m_chatConfig}});
}

void AlertsDock::onShown(const ChatMessage &msg)
{
	if (!m_enabled || !ChatOverlay::passes(m_chatConfig, msg))
		return;
	m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat")},
					{QStringLiteral("message"), ChatOverlay::toJson(msg)}});
}

void AlertsDock::onActivity(const ChatMessage &msg, const QString &description)
{
	if (!m_enabled)
		return;
	const Alerts::Event event = Alerts::fromChat(msg);
	if (event.type.isEmpty() || m_chatDedup.swallow(event, QDateTime::currentMSecsSinceEpoch()) ||
	    !ChatOverlay::passesEvent(m_chatConfig, event))
		return;
	m_server->broadcast(
		QJsonObject{{QStringLiteral("type"), QStringLiteral("chat-event")},
			    {QStringLiteral("event"), ChatOverlay::eventToJson(event, description, msg.id)}});
}

namespace {

/* Made-up events for the editors' previews and "test" buttons. */
QList<ChatMessage> sampleEventMessages()
{
	const auto make = [](ChatPlatform platform, ChatEvent kind, const char *who, int amount, const char *detail,
			     const char *text) {
		ChatMessage m{platform, QString::fromUtf8(who), QString(), QString::fromUtf8(text), QString()};
		m.event = kind;
		m.amount = amount;
		m.detail = QString::fromUtf8(detail);
		return m;
	};
	return {make(ChatPlatform::Twitch, ChatEvent::Sub, "Texuguito", 3, "1000", "três meses!"),
		make(ChatPlatform::Kick, ChatEvent::GiftSub, "Generoso", 5, "", ""),
		make(ChatPlatform::Twitch, ChatEvent::Raid, "Vizinha", 42, "", ""),
		make(ChatPlatform::YouTube, ChatEvent::Donation, "@Fulana", 0, "R$ 10,00", "valeu pela live!"),
		make(ChatPlatform::YouTube, ChatEvent::Membership, "@Ciclano", 1, "", ""),
		make(ChatPlatform::Twitch, ChatEvent::Bits, "Bia", 500, "", "toma!"),
		make(ChatPlatform::Twitch, ChatEvent::Follow, "novato", 0, "", "")};
}

} // namespace

QJsonObject AlertsDock::eventsSamples() const
{
	EventsOverlay::History sample;
	for (const ChatMessage &m : sampleEventMessages()) {
		const Alerts::Event event = Alerts::fromChat(m);
		if (!event.type.isEmpty())
			sample.add(EventsOverlay::entryFrom(event, UnifiedChatDock::describeEvent(m),
							    QStringLiteral("sample-") + event.type, 0));
	}
	return QJsonObject{{QStringLiteral("recent"), sample.recent(EventsOverlay::History::kKeep)},
			   {QStringLiteral("labels"), sample.labels()}};
}

QJsonArray AlertsDock::chatEventSamples() const
{
	QJsonArray out;
	for (const ChatMessage &m : sampleEventMessages()) {
		const Alerts::Event event = Alerts::fromChat(m);
		if (!event.type.isEmpty())
			out.append(ChatOverlay::eventToJson(event, UnifiedChatDock::describeEvent(m),
							    QStringLiteral("sample-") + event.type));
	}
	return out;
}

void AlertsDock::onRemoved(ChatPlatform platform, const QString &messageId, const QString &userId, bool all)
{
	if (!m_enabled)
		return;
	m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat-remove")},
					{QStringLiteral("platform"), ChatOverlay::platformKey(platform)},
					{QStringLiteral("id"), messageId},
					{QStringLiteral("user"), userId},
					{QStringLiteral("all"), all}});
}

void AlertsDock::saveConfig()
{
	QSaveFile file(QDir(m_dir).filePath(QStringLiteral("alerts.json")));
	if (!file.open(QIODevice::WriteOnly) ||
	    file.write(QJsonDocument(m_config).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
		obs_log(LOG_WARNING, "[alerts] could not save alerts.json");
}

QJsonObject AlertsDock::overlayConfig() const
{
	QJsonObject config = Alerts::forOverlay(m_config);
	config.insert(QStringLiteral("lang"), language());
	return config;
}

void AlertsDock::broadcastConfig()
{
	m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("config")},
					{QStringLiteral("config"), overlayConfig()}});
}

QJsonObject AlertsDock::shareableConfig() const
{
	return Alerts::forOverlay(m_config);
}

void AlertsDock::importConfig(const QJsonObject &config)
{
	/* Keys for GIF search stay the ones typed on this computer. */
	QJsonObject merged = config;
	merged.insert(QStringLiteral("integrations"), m_config.value(QStringLiteral("integrations")));
	m_config = Alerts::normalize(merged, T);
	saveConfig();
	broadcastConfig();
}

void AlertsDock::applyEnabled()
{
	if (m_enabled) {
		if (!m_server->isListening() && !m_server->listen(m_port))
			obs_log(LOG_WARNING, "[alerts] port %d is in use, alerts not available", m_port);
	} else {
		m_server->close();
	}
	m_toggle->setText(m_enabled ? T("Alerts.TurnOff") : T("Alerts.TurnOn"));
	refreshStatus();
}

void AlertsDock::refreshStatus()
{
	if (!m_enabled)
		m_status->setText(T("Alerts.Off"));
	else if (!m_server->isListening())
		m_status->setText(QStringLiteral("<span style=\"color:#E03C3C\">%1</span>")
					  .arg(T("Alerts.PortInUse").arg(m_port).toHtmlEscaped()));
	else
		m_status->setText(T("Alerts.Status").arg(overlayUrl().toHtmlEscaped()).arg(m_server->clientCount()));
}

void AlertsDock::onChat(const ChatMessage &msg)
{
	if (!m_enabled)
		return;
	widgetsChat(msg);
	if (msg.event == ChatEvent::None)
		return;
	const Alerts::Event event = Alerts::fromChat(msg);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	/* One gift dedup for alerts and history, so both see the same events. */
	if (event.type.isEmpty() || m_dedup.swallow(event, now))
		return;
	m_history.add(EventsOverlay::entryFrom(event, UnifiedChatDock::describeEvent(msg), msg.id, now));
	saveHistory();
	broadcastHistory();
	widgetsEvent(event);
	if (Alerts::passes(m_config, event))
		fire(event);
}

void AlertsDock::fire(const Alerts::Event &event)
{
	const QJsonObject type = m_config.value(QStringLiteral("types")).toObject().value(event.type).toObject();
	QJsonObject message{{QStringLiteral("type"), QStringLiteral("alert")},
			    {QStringLiteral("alert"), Alerts::toJson(event)}};
	if (!type.value(QStringLiteral("tts")).toBool() || event.message.isEmpty()) {
		m_server->broadcast(message);
		return;
	}
	QPointer<AlertsDock> self = this;
	GoogleTts::synthesize(
		&m_net, event.message.left(kTtsMaxChars),
		[self, message](QByteArray mp3, QString error) mutable {
			if (!self)
				return;
			if (!mp3.isEmpty()) {
				const QString id = QStringLiteral("a%1.mp3").arg(++self->m_ttsSerial);
				self->m_tts.insert(id, mp3);
				self->m_ttsOrder.append(id);
				while (self->m_ttsOrder.size() > kTtsKeep)
					self->m_tts.remove(self->m_ttsOrder.takeFirst());
				message.insert(QStringLiteral("tts"), QStringLiteral("/tts/") + id);
			} else {
				obs_log(LOG_INFO, "[alerts] TTS failed: %s", error.toUtf8().constData());
			}
			self->m_server->broadcast(message);
		},
		this, language());
}

bool AlertsDock::route(const OverlayServer::Request &request, OverlayServer::Reply &reply)
{
	const QString &path = request.path;
	const bool get = request.method == "GET";
	if (get && path == QLatin1String("/alertas")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("alerts.html"));
		return true;
	}
	if (get && path == QLatin1String("/editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("editor.html"));
		return true;
	}
	if (get && path == QLatin1String("/chat")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("chat.html"));
		return true;
	}
	if (get && path == QLatin1String("/chat-editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("chat-editor.html"));
		return true;
	}
	if (get && path == QLatin1String("/painel")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("panel.html"));
		return true;
	}
	if (get && path == QLatin1String("/bot-editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("bot-editor.html"));
		return true;
	}
	if (get && path == QLatin1String("/desfile-editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("parade-editor.html"));
		return true;
	}
	if (get && path == QLatin1String("/tocando-editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("nowplaying-editor.html"));
		return true;
	}
	if (get && path == QLatin1String("/eventos")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("events.html"));
		return true;
	}
	if (get && path == QLatin1String("/eventos-editor")) {
		reply.file = QDir(webDir()).filePath(QStringLiteral("events-editor.html"));
		return true;
	}
	if (get && widgetsPage(path, reply))
		return true;
	if (get && path.startsWith(QLatin1String("/media/"))) {
		reply.file = OverlayServer::resolvePath(m_mediaDir, path.mid(7));
		if (reply.file.isEmpty())
			errorReply(reply, 404, QStringLiteral("not found"));
		return true;
	}
	if (!path.startsWith(QLatin1String("/api/")))
		return false;

	/* Other sites open in the same browser can reach localhost too: only the
	 * editor page, which got the token from the dock, may use the API (the
	 * strings are public, so the page can say the token is wrong). */
	const bool open = path == QLatin1String("/api/i18n");
	if (!Alerts::isLocalHost(request.headers.value("host"), m_port) ||
	    !Alerts::isLocalOrigin(request.headers.value("origin"), m_port) ||
	    (!open && request.headers.value("x-token") != m_token.toLatin1())) {
		errorReply(reply, 403, QStringLiteral("forbidden"));
		return true;
	}

	if (widgetsApi(request, reply))
		return true;
	const QUrlQuery query(request.query);
	const bool post = request.method == "POST";
	if (get && path == QLatin1String("/api/config")) {
		QJsonObject config = m_config;
		config.insert(QStringLiteral("lang"), language());
		jsonReply(reply, 200, config);
	} else if (post && path == QLatin1String("/api/config")) {
		const QJsonDocument doc = QJsonDocument::fromJson(request.body);
		if (!doc.isObject()) {
			errorReply(reply, 400, QStringLiteral("bad json"));
			return true;
		}
		m_config = Alerts::normalize(doc.object(), T);
		saveConfig();
		broadcastConfig();
		jsonReply(reply, 200, m_config);
	} else if (post && path == QLatin1String("/api/test")) {
		const QString type = query.queryItemValue(QStringLiteral("type"));
		if (!Alerts::isType(type)) {
			errorReply(reply, 400, QStringLiteral("unknown type"));
			return true;
		}
		fire(Alerts::sample(type, T));
		jsonReply(reply, 200, QJsonObject{{QStringLiteral("overlays"), m_server->clientCount()}});
	} else if (get && path == QLatin1String("/api/chat-config")) {
		jsonReply(reply, 200, m_chatConfig);
	} else if (post && path == QLatin1String("/api/chat-config")) {
		const QJsonDocument doc = QJsonDocument::fromJson(request.body);
		if (!doc.isObject()) {
			errorReply(reply, 400, QStringLiteral("bad json"));
			return true;
		}
		importChatConfig(doc.object());
		jsonReply(reply, 200, m_chatConfig);
	} else if (path.startsWith(QLatin1String("/api/tab/"))) {
		const auto tab = g_panelTabs.constFind(path.mid(9));
		if (tab == g_panelTabs.constEnd()) {
			errorReply(reply, 404, QStringLiteral("not found"));
		} else if (get) {
			jsonReply(reply, 200, tab->get());
		} else if (post) {
			const QJsonDocument doc = QJsonDocument::fromJson(request.body);
			if (!doc.isObject())
				errorReply(reply, 400, QStringLiteral("bad json"));
			else
				jsonReply(reply, 200, tab->set(doc.object()));
		} else {
			errorReply(reply, 404, QStringLiteral("not found"));
		}
	} else if (get && path == QLatin1String("/api/events-config")) {
		jsonReply(reply, 200, m_eventsConfig);
	} else if (post && path == QLatin1String("/api/events-config")) {
		const QJsonDocument doc = QJsonDocument::fromJson(request.body);
		if (!doc.isObject()) {
			errorReply(reply, 400, QStringLiteral("bad json"));
			return true;
		}
		importEventsConfig(doc.object());
		jsonReply(reply, 200, m_eventsConfig);
	} else if (get && path == QLatin1String("/api/events-sample")) {
		jsonReply(reply, 200, eventsSamples());
	} else if (post && path == QLatin1String("/api/events-test")) {
		/* Shown by the open pages only: the history and the labels stay real. */
		const QJsonArray recent = eventsSamples().value(QStringLiteral("recent")).toArray();
		QJsonObject entry =
			recent.at(QRandomGenerator::global()->bounded(static_cast<int>(recent.size()))).toObject();
		entry.insert(QStringLiteral("id"), QStringLiteral("test-%1").arg(QDateTime::currentMSecsSinceEpoch()));
		m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("events-test")},
						{QStringLiteral("entry"), entry}});
		jsonReply(reply, 200, QJsonObject{{QStringLiteral("overlays"), m_server->clientCount()}});
	} else if (post && path == QLatin1String("/api/events-reset")) {
		m_history.resetTop();
		saveHistory();
		broadcastHistory();
		jsonReply(reply, 200, QJsonObject{{QStringLiteral("ok"), true}});
	} else if (get && path == QLatin1String("/api/chat-sample")) {
		jsonReply(reply, 200,
			  QJsonObject{{QStringLiteral("messages"), ChatOverlay::samples()},
				      {QStringLiteral("events"), chatEventSamples()}});
	} else if (post && path == QLatin1String("/api/chat-test")) {
		/* One made-up line (or event), past the filters: the page still
		 * picks the platforms. */
		const bool event = query.queryItemValue(QStringLiteral("kind")) == QLatin1String("event");
		const QJsonArray samples = event ? chatEventSamples() : ChatOverlay::samples();
		QJsonObject item =
			samples.at(QRandomGenerator::global()->bounded(static_cast<int>(samples.size()))).toObject();
		item.insert(QStringLiteral("id"), QStringLiteral("test-%1").arg(QDateTime::currentMSecsSinceEpoch()));
		m_server->broadcast(QJsonObject{{QStringLiteral("type"),
						 event ? QStringLiteral("chat-event") : QStringLiteral("chat")},
						{event ? QStringLiteral("event") : QStringLiteral("message"), item}});
		jsonReply(reply, 200, QJsonObject{{QStringLiteral("overlays"), m_server->clientCount()}});
	} else if (get && path == QLatin1String("/api/sample")) {
		/* The editor's preview uses the same made-up events. */
		QJsonObject samples;
		for (const QString &type : Alerts::types())
			samples.insert(type, Alerts::toJson(Alerts::sample(type, T)));
		jsonReply(reply, 200, samples);
	} else if (get && path == QLatin1String("/api/i18n")) {
		/* Every Alerts.* string, in the language OBS is in (en-US has
		 * all the keys). */
		QJsonObject strings;
		char *ini = obs_module_file("locale/en-US.ini");
		QFile file(QString::fromUtf8(ini ? ini : ""));
		bfree(ini);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			while (!file.atEnd()) {
				const QByteArray line = file.readLine();
				const qsizetype eq = line.indexOf('=');
				if (eq > 0 && (line.startsWith("Alerts.") || line.startsWith("ChatOverlay.") ||
					       line.startsWith("EventsOverlay.") || line.startsWith("NowPlaying.") ||
					       line.startsWith("Overlays.") || line.startsWith("Texuguito.Parade.") ||
					       line.startsWith("Texuguito.BotPanel.") || line.startsWith("Goals.") ||
					       line.startsWith("Poll.") || line.startsWith("Subathon.") ||
					       line.startsWith("Theme."))) {
					const QByteArray key = line.left(eq).trimmed();
					strings.insert(QString::fromUtf8(key), T(key.constData()));
				}
			}
		}
		strings.insert(QStringLiteral("lang"), language());
		jsonReply(reply, 200, strings);
	} else if (get && path == QLatin1String("/api/media")) {
		apiMediaList(reply);
	} else if (post && path == QLatin1String("/api/media")) {
		apiMediaUpload(query.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded), request.body, reply);
	} else if (post && path == QLatin1String("/api/media/delete")) {
		const QString file = OverlayServer::resolvePath(m_mediaDir, query.queryItemValue(QStringLiteral("name"),
												 QUrl::FullyEncoded));
		if (file.isEmpty() || !QFile::remove(file))
			errorReply(reply, 404, QStringLiteral("not found"));
		else
			jsonReply(reply, 200, QJsonObject{{QStringLiteral("ok"), true}});
	} else {
		errorReply(reply, 404, QStringLiteral("not found"));
	}
	return true;
}

void AlertsDock::apiMediaList(OverlayServer::Reply &reply) const
{
	QJsonArray files;
	const QFileInfoList entries = QDir(m_mediaDir).entryInfoList(QDir::Files, QDir::Time);
	for (const QFileInfo &info : entries) {
		const QString kind = Alerts::mediaKind(info.fileName());
		if (kind.isEmpty() || Alerts::safeMediaName(info.fileName()) != info.fileName())
			continue;
		files.append(QJsonObject{{QStringLiteral("name"), info.fileName()},
					 {QStringLiteral("kind"), kind},
					 {QStringLiteral("size"), info.size()}});
	}
	jsonReply(reply, 200, QJsonObject{{QStringLiteral("files"), files}});
}

void AlertsDock::apiMediaUpload(const QString &name, const QByteArray &data, OverlayServer::Reply &reply)
{
	const QString safe = Alerts::safeMediaName(name);
	if (safe.isEmpty() || data.isEmpty()) {
		errorReply(reply, 400, QStringLiteral("unsupported file"));
		return;
	}
	const QFileInfo info(safe);
	QString target = safe;
	for (int n = 2; QFile::exists(QDir(m_mediaDir).filePath(target)); n++)
		target = QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix());
	QSaveFile file(QDir(m_mediaDir).filePath(target));
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
		errorReply(reply, 500, QStringLiteral("could not save"));
		return;
	}
	jsonReply(reply, 200,
		  QJsonObject{{QStringLiteral("name"), target}, {QStringLiteral("kind"), Alerts::mediaKind(target)}});
}

void AlertsDock::addBrowserSource(const QString &title, const QString &url, int width, int height, bool audio)
{
	const QByteArray name = title.toUtf8();
	obs_source_t *source = obs_get_source_by_name(name.constData());
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", url.toUtf8().constData());
		obs_data_set_int(settings, "width", width);
		obs_data_set_int(settings, "height", height);
		/* Alert sounds show up in the OBS mixer like any other source. */
		obs_data_set_bool(settings, "reroute_audio", audio);
		source = obs_source_create("browser_source", name.constData(), settings, nullptr);
		obs_data_release(settings);
	}
	if (!source) {
		QMessageBox::information(this, T("Alerts.Title"), T("Alerts.NoBrowser").arg(url));
		return;
	}
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene)
		obs_scene_add(scene, source);
	obs_source_release(sceneSource);
	obs_source_release(source);
}

void AlertsDock::openSettings()
{
	QDialog dialog(this);
	dialog.setWindowTitle(T("Alerts.Title"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout();
	auto *port = new QSpinBox(&dialog);
	port->setRange(1024, 65535);
	port->setValue(m_port);
	form->addRow(T("Alerts.Port"), port);
	layout->addLayout(form);
	auto *note = new QLabel(T("Alerts.PortHint"), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const auto newPort = static_cast<quint16>(port->value());
	if (newPort != m_port) {
		m_port = newPort;
		m_server->close();
	}
	saveSettings();
	applyEnabled();
}

void alerts_register(void)
{
	UnifiedChatDock *chat = unifiedChatDock();
	if (!chat) {
		obs_log(LOG_WARNING, "[alerts] Unified Chat is not available, alerts stay off");
		return;
	}
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new AlertsDock(chat, main);
	auto *scroll = new QScrollArea(main);
	scroll->setWidget(dock);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setMinimumWidth(dock->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
	/* "Overlays" now (alerts, chat on screen, events); same id, so the panel
	 * keeps its place in saved layouts. */
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Overlays.Title"), scroll)) {
		obs_log(LOG_WARNING, "[alerts] could not add dock");
		delete scroll;
		return;
	}
	g_dock = dock;

	configShareAddSection(
		{QStringLiteral("alerts"), "Config.Section.Alerts",
		 []() { return g_dock ? QJsonValue(g_dock->shareableConfig()) : QJsonValue(); },
		 [](const QJsonValue &v) {
			 if (g_dock && v.isObject())
				 g_dock->importConfig(v.toObject());
		 },
		 [](const QJsonValue &v) {
			 const QJsonObject types = v.toObject().value(QStringLiteral("types")).toObject();
			 int on = 0;
			 for (const QString &type : Alerts::types())
				 on += types.value(type).toObject().value(QStringLiteral("enabled")).toBool() ? 1 : 0;
			 return T("Alerts.Describe").arg(on).arg(Alerts::types().size());
		 }});
	configShareAddSection({QStringLiteral("eventsOverlay"), "Config.Section.EventsOverlay",
			       []() { return g_dock ? QJsonValue(g_dock->eventsConfig()) : QJsonValue(); },
			       [](const QJsonValue &v) {
				       if (g_dock && v.isObject())
					       g_dock->importEventsConfig(v.toObject());
			       },
			       [](const QJsonValue &) {
				       return T("EventsOverlay.Describe");
			       }});
	configShareAddSection(
		{QStringLiteral("chatOverlay"), "Config.Section.ChatOverlay",
		 []() { return g_dock ? QJsonValue(g_dock->chatConfig()) : QJsonValue(); },
		 [](const QJsonValue &v) {
			 if (g_dock && v.isObject())
				 g_dock->importChatConfig(v.toObject());
		 },
		 [](const QJsonValue &v) {
			 const QJsonObject on = v.toObject().value(QStringLiteral("platforms")).toObject();
			 QStringList names;
			 for (const QString &p : ChatOverlay::platforms()) {
				 if (on.value(p).toBool(true))
					 names.append(p == QLatin1String("twitch")    ? QStringLiteral("Twitch")
						      : p == QLatin1String("youtube") ? QStringLiteral("YouTube")
										      : QStringLiteral("Kick"));
			 }
			 return T("ChatOverlay.Describe").arg(names.join(QStringLiteral(", ")));
		 }});
}

void alerts_unregister(void) {}
