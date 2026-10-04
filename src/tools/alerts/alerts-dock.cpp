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

} // namespace

AlertsDock::AlertsDock(UnifiedChatDock *chat, QWidget *parent) : QWidget(parent), m_chat(chat)
{
	m_dir = moduleConfigDir();
	m_mediaDir = QDir(m_dir).filePath(QStringLiteral("media"));
	QDir().mkpath(m_mediaDir);
	loadSettings();
	loadConfig();

	OverlayServer::Routes routes;
	routes.webDir = webDir();
	routes.ttsClip = [this](const QString &id) {
		return m_tts.value(id);
	};
	routes.snapshot = [this]() {
		/* Alert pages read "config", chat pages "chat". */
		return QJsonObject{{QStringLiteral("type"), QStringLiteral("config")},
				   {QStringLiteral("config"), overlayConfig()},
				   {QStringLiteral("chat"), m_chatConfig}};
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
	connect(m_chat, &UnifiedChatDock::removed, this, &AlertsDock::onRemoved);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);

	auto *row1 = new QHBoxLayout();
	m_toggle = new QPushButton(this);
	connect(m_toggle, &QPushButton::clicked, this, [this]() {
		m_enabled = !m_enabled;
		saveSettings();
		applyEnabled();
	});
	auto *customize = new QPushButton(T("Alerts.Customize"), this);
	connect(customize, &QPushButton::clicked, this, [this]() {
		if (!m_server->isListening()) {
			refreshStatus();
			return;
		}
		QDesktopServices::openUrl(QUrl(editorUrl()));
	});
	auto *add = new QPushButton(T("Alerts.AddSource"), this);
	connect(add, &QPushButton::clicked, this, [this]() {
		obs_video_info ovi{};
		obs_get_video_info(&ovi);
		addBrowserSource(T("Alerts.SourceName"), overlayUrl(), static_cast<int>(ovi.base_width),
				 static_cast<int>(ovi.base_height), true);
	});
	row1->addWidget(m_toggle);
	row1->addWidget(customize);
	row1->addWidget(add);
	layout->addLayout(row1);

	auto *row2 = new QHBoxLayout();
	auto *test = new QToolButton(this);
	test->setText(T("Alerts.Test"));
	test->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(test);
	for (const QString &type : Alerts::types()) {
		const QByteArray key = "Alerts.Type." + type.toLatin1();
		menu->addAction(T(key.constData()), this, [this, type]() { fire(Alerts::sample(type, T)); });
	}
	test->setMenu(menu);
	auto *skip = new QPushButton(T("Alerts.Skip"), this);
	connect(skip, &QPushButton::clicked, this,
		[this]() { m_server->broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("skip")}}); });
	auto *copy = new QPushButton(T("Alerts.CopyUrl"), this);
	connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(overlayUrl()); });
	auto *settings = new QToolButton(this);
	settings->setText(T("UnifiedChat.Settings"));
	connect(settings, &QToolButton::clicked, this, &AlertsDock::openSettings);
	row2->addWidget(test);
	row2->addWidget(skip);
	row2->addWidget(copy);
	row2->addWidget(settings);
	layout->addLayout(row2);

	auto *help = new QLabel(T("Alerts.Help"), this);
	help->setWordWrap(true);
	help->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(help);

	/* Chat on screen shares the server, the token and the editor look. */
	auto *chatTitle = new QLabel(QStringLiteral("<b>%1</b>").arg(T("ChatOverlay.Title").toHtmlEscaped()), this);
	layout->addSpacing(6);
	layout->addWidget(chatTitle);
	auto *row3 = new QHBoxLayout();
	auto *chatCustomize = new QPushButton(T("Alerts.Customize"), this);
	connect(chatCustomize, &QPushButton::clicked, this, [this]() {
		if (!m_server->isListening()) {
			refreshStatus();
			return;
		}
		QDesktopServices::openUrl(QUrl(chatEditorUrl()));
	});
	auto *chatAdd = new QPushButton(T("Alerts.AddSource"), this);
	connect(chatAdd, &QPushButton::clicked, this,
		[this]() { addBrowserSource(T("ChatOverlay.SourceName"), chatOverlayUrl(), 480, 720, false); });
	auto *chatCopy = new QPushButton(T("Alerts.CopyUrl"), this);
	connect(chatCopy, &QPushButton::clicked, this,
		[this]() { QApplication::clipboard()->setText(chatOverlayUrl()); });
	row3->addWidget(chatCustomize);
	row3->addWidget(chatAdd);
	row3->addWidget(chatCopy);
	layout->addLayout(row3);
	auto *chatHelp = new QLabel(T("ChatOverlay.Help"), this);
	chatHelp->setWordWrap(true);
	chatHelp->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(chatHelp);
	layout->addStretch();

	applyEnabled();
}

AlertsDock::~AlertsDock()
{
	m_server->close();
}

QString AlertsDock::overlayUrl() const
{
	return QStringLiteral("http://localhost:%1/alertas").arg(m_port);
}

QString AlertsDock::editorUrl() const
{
	/* The token goes after "#": browsers never send that part anywhere. */
	return QStringLiteral("http://localhost:%1/editor#t=%2").arg(m_port).arg(m_token);
}

QString AlertsDock::chatOverlayUrl() const
{
	return QStringLiteral("http://localhost:%1/chat").arg(m_port);
}

QString AlertsDock::chatEditorUrl() const
{
	return QStringLiteral("http://localhost:%1/chat-editor#t=%2").arg(m_port).arg(m_token);
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

QJsonArray AlertsDock::chatEventSamples() const
{
	const auto make = [](ChatPlatform platform, ChatEvent kind, const char *who, int amount, const char *detail,
			     const char *text) {
		ChatMessage m{platform, QString::fromUtf8(who), QString(), QString::fromUtf8(text), QString()};
		m.event = kind;
		m.amount = amount;
		m.detail = QString::fromUtf8(detail);
		return m;
	};
	QJsonArray out;
	for (const ChatMessage &m :
	     {make(ChatPlatform::Twitch, ChatEvent::Sub, "Texuguito", 3, "1000", "três meses!"),
	      make(ChatPlatform::Kick, ChatEvent::GiftSub, "Generoso", 5, "", ""),
	      make(ChatPlatform::Twitch, ChatEvent::Raid, "Vizinha", 42, "", ""),
	      make(ChatPlatform::YouTube, ChatEvent::Donation, "@Fulana", 0, "R$ 10,00", "valeu pela live!"),
	      make(ChatPlatform::YouTube, ChatEvent::Membership, "@Ciclano", 1, "", ""),
	      make(ChatPlatform::Twitch, ChatEvent::Follow, "novato", 0, "", "")}) {
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
	if (!m_enabled || msg.event == ChatEvent::None)
		return;
	const Alerts::Event event = Alerts::fromChat(msg);
	if (event.type.isEmpty() || m_dedup.swallow(event, QDateTime::currentMSecsSinceEpoch()) ||
	    !Alerts::passes(m_config, event))
		return;
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
				if (eq > 0 && (line.startsWith("Alerts.") || line.startsWith("ChatOverlay."))) {
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
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Alerts.Title"), scroll)) {
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
