/*
Meketreve OBS Essentials - Texuguito bot
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
#include "texuguito-dock.hpp"
#include "../alerts/alerts-dock.hpp"
#include "texuguito.h"
#include "sound-fetch.hpp"
#include "tts-client.hpp"

#include "../unified-chat/chat-accounts.hpp"
#include "../unified-chat/kick-chat.hpp"
#include "../unified-chat/twitch-chat.hpp"
#include "../unified-chat/unified-chat-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr const char *kDockId = "meketreve-texuguito";
constexpr const char *kSourceName = "Texuguito";
constexpr int kChattersPollMs = 45000;
constexpr qint64 kEchoWindowMs = 60000;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString moduleConfigDir()
{
	char *dir = obs_module_config_path("texuguito");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	QDir().mkpath(path);
	return path;
}

QString webDir()
{
	char *dir = obs_module_file("texuguito/web");
	const QString path = QString::fromUtf8(dir ? dir : "");
	bfree(dir);
	return path;
}

/* Both bot languages (Key="value" lines), so replies follow the command's
 * language instead of the OBS language. */
QHash<QString, QString> readBotLocale(const char *name)
{
	QHash<QString, QString> texts;
	const QByteArray path = QByteArray("locale/") + name;
	char *ini = obs_module_file(path.constData());
	QFile file(QString::fromUtf8(ini ? ini : ""));
	bfree(ini);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return texts;
	while (!file.atEnd()) {
		const QString line = QString::fromUtf8(file.readLine()).trimmed();
		const qsizetype eq = line.indexOf(QLatin1Char('='));
		if (eq <= 0 || line.startsWith(QLatin1Char('#')))
			continue;
		QString value = line.mid(eq + 1);
		if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
			value = value.mid(1, value.size() - 2);
		texts.insert(line.left(eq), value.replace(QStringLiteral("\\\""), QStringLiteral("\"")));
	}
	return texts;
}

/* Copies <from> into <to>, keeping a .bak of anything it replaces. */
int copyTree(const QString &from, const QString &to)
{
	int copied = 0;
	QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString src = it.next();
		const QString dst = QDir(to).filePath(QDir(from).relativeFilePath(src));
		QDir().mkpath(QFileInfo(dst).absolutePath());
		if (QFile::exists(dst)) {
			QFile::remove(dst + QStringLiteral(".bak"));
			QFile::rename(dst, dst + QStringLiteral(".bak"));
		}
		if (QFile::copy(src, dst))
			copied++;
	}
	return copied;
}

QPointer<TexuguitoDock> g_dock;

} // namespace

TexuguitoDock::TexuguitoDock(UnifiedChatDock *chat, QWidget *parent) : QWidget(parent), m_chat(chat)
{
	m_dataDir = moduleConfigDir();
	m_audioDir = QDir(m_dataDir).filePath(QStringLiteral("audios"));
	loadSettings();

	m_engine = new BotEngine(m_dataDir, m_audioDir, this);
	m_engine->setVolume(m_volume);
	m_engine->setClipCooldowns(m_cooldowns);
	m_engine->setText([](const char *key) { return T(key); });
	m_engine->setCommandTexts(readBotLocale("en-US.ini"), readBotLocale("pt-BR.ini"));
	m_engine->setTts(
		[this](const QString &text, const QString &lang, std::function<void(QByteArray, QString)> done) {
			GoogleTts::synthesize(&m_net, text, std::move(done), this, lang);
		});
	m_engine->setSoundFetch([this](const QString &link, std::function<void(const SoundFetch::Result &)> done) {
		SoundFetch::fetch(&m_net, link, std::move(done), this);
	});

	OverlayServer::Routes routes;
	routes.webDir = webDir();
	routes.audioDir = [this]() {
		return m_engine->audioDir();
	};
	routes.ttsClip = [this](const QString &id) {
		return m_engine->ttsClip(id);
	};
	routes.snapshot = [this]() {
		QJsonObject snapshot = m_engine->snapshot();
		snapshot.insert(QStringLiteral("look"), lookJson());
		return snapshot;
	};
	m_server = new OverlayServer(routes, this);

	connect(m_engine, &BotEngine::overlayMessage, m_server, &OverlayServer::broadcast);
	connect(m_engine, &BotEngine::reply, this, &TexuguitoDock::onReply);
	connect(m_server, &OverlayServer::clientsChanged, this, [this](int n) {
		m_engine->setOverlayListeners(n);
		refreshStatus();
	});
	connect(m_chat, &UnifiedChatDock::incoming, this, &TexuguitoDock::onChat);
	connect(m_chat, &UnifiedChatDock::targetsChanged, this, [this]() {
		updateStreamerChannels();
		refreshStatus();
		pollChatters();
	});
	updateStreamerChannels();
	connect(m_chat->accounts(), &ChatAccounts::accountChanged, this, [this]() {
		m_chattersDenied = false;
		refreshStatus();
		pollChatters();
	});

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	m_replies = new QLabel(this);
	m_replies->setWordWrap(true);
	layout->addWidget(m_replies);

	auto *row1 = new QHBoxLayout();
	m_toggle = new QPushButton(this);
	connect(m_toggle, &QPushButton::clicked, this, [this]() {
		m_enabled = !m_enabled;
		saveSettings();
		applyEnabled();
	});
	/* The parade's "Add to scene" and link are in the Overlays panel. */
	row1->addWidget(m_toggle);
	layout->addLayout(row1);

	auto *row2 = new QHBoxLayout();
	auto *audios = new QPushButton(T("Texuguito.OpenAudios"), this);
	connect(audios, &QPushButton::clicked, this, [this]() {
		QDir().mkpath(m_engine->audioDir());
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_engine->audioDir()));
	});
	auto *addAudio = new QPushButton(T("Texuguito.AddAudio"), this);
	connect(addAudio, &QPushButton::clicked, this, &TexuguitoDock::addAudio);
	auto *import = new QPushButton(T("Texuguito.Import"), this);
	connect(import, &QPushButton::clicked, this, &TexuguitoDock::importOldBot);
	auto *settings = new QToolButton(this);
	settings->setText(T("UnifiedChat.Settings"));
	connect(settings, &QToolButton::clicked, this, &TexuguitoDock::openSettings);
	row2->addWidget(audios);
	row2->addWidget(addAudio);
	layout->addLayout(row2);
	auto *row3 = new QHBoxLayout();
	row3->addWidget(import);
	row3->addWidget(settings);
	layout->addLayout(row3);

	auto *help = new QLabel(T("Texuguito.Help"), this);
	help->setWordWrap(true);
	help->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(help);
	layout->addStretch();

	m_chattersTimer.setInterval(kChattersPollMs);
	connect(&m_chattersTimer, &QTimer::timeout, this, &TexuguitoDock::pollChatters);

	applyEnabled();
}

TexuguitoDock::~TexuguitoDock()
{
	m_server->close();
}

QString TexuguitoDock::statusText() const
{
	return m_status->text() + QStringLiteral(" | ") + m_replies->text();
}

QString TexuguitoDock::overlayUrl() const
{
	return QStringLiteral("http://localhost:%1/overlay").arg(m_port);
}

QJsonObject TexuguitoDock::lookJson() const
{
	return QJsonObject{{QStringLiteral("scale"), m_scale},
			   {QStringLiteral("speed"), m_speed},
			   {QStringLiteral("names"), m_names},
			   {QStringLiteral("nameSize"), m_nameSize}};
}

QJsonObject TexuguitoDock::panelState() const
{
	QJsonObject state = lookJson();
	state.insert(QStringLiteral("url"), overlayUrl());
	state.insert(QStringLiteral("listening"), m_server->isListening());
	return state;
}

QJsonObject TexuguitoDock::applyPanel(const QJsonObject &panel)
{
	m_scale = std::clamp(panel.value(QStringLiteral("scale")).toDouble(m_scale), 0.5, 4.0);
	m_speed = std::clamp(panel.value(QStringLiteral("speed")).toDouble(m_speed), 0.25, 3.0);
	m_names = panel.value(QStringLiteral("names")).toBool(m_names);
	m_nameSize = std::clamp(panel.value(QStringLiteral("nameSize")).toInt(m_nameSize), 6, 32);
	saveSettings();
	m_server->broadcast(
		QJsonObject{{QStringLiteral("type"), QStringLiteral("look")}, {QStringLiteral("look"), lookJson()}});
	return panelState();
}

void TexuguitoDock::loadSettings()
{
	const QString path = QDir(m_dataDir).filePath(QStringLiteral("settings.json"));
	obs_data_t *data = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
	if (!data)
		return;
	obs_data_set_default_bool(data, "enabled", true);
	obs_data_set_default_int(data, "port", 8901);
	obs_data_set_default_double(data, "volume", 1.0);
	m_enabled = obs_data_get_bool(data, "enabled");
	m_port = static_cast<quint16>(obs_data_get_int(data, "port"));
	m_volume = obs_data_get_double(data, "volume");
	obs_data_set_default_double(data, "paradeScale", 1.0);
	obs_data_set_default_double(data, "paradeSpeed", 1.0);
	obs_data_set_default_bool(data, "paradeNames", true);
	obs_data_set_default_int(data, "paradeNameSize", 10);
	m_scale = std::clamp(obs_data_get_double(data, "paradeScale"), 0.5, 4.0);
	m_speed = std::clamp(obs_data_get_double(data, "paradeSpeed"), 0.25, 3.0);
	m_names = obs_data_get_bool(data, "paradeNames");
	m_nameSize = std::clamp(static_cast<int>(obs_data_get_int(data, "paradeNameSize")), 6, 32);
	const QString audio = QString::fromUtf8(obs_data_get_string(data, "audioDir"));
	if (!audio.isEmpty())
		m_audioDir = audio;
	obs_data_t *cooldowns = obs_data_get_obj(data, "cooldowns");
	for (obs_data_item_t *item = obs_data_first(cooldowns); item; obs_data_item_next(&item)) {
		bool ok = false;
		const int cost = QString::fromUtf8(obs_data_item_get_name(item)).toInt(&ok);
		if (ok)
			m_cooldowns.insert(cost, static_cast<int>(obs_data_item_get_int(item)));
	}
	obs_data_release(cooldowns);
	obs_data_release(data);
}

void TexuguitoDock::saveSettings()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", m_enabled);
	obs_data_set_int(data, "port", m_port);
	obs_data_set_double(data, "volume", m_volume);
	obs_data_set_double(data, "paradeScale", m_scale);
	obs_data_set_double(data, "paradeSpeed", m_speed);
	obs_data_set_bool(data, "paradeNames", m_names);
	obs_data_set_int(data, "paradeNameSize", m_nameSize);
	obs_data_set_string(data, "audioDir", m_engine->audioDir().toUtf8().constData());
	obs_data_t *cooldowns = obs_data_create();
	for (auto it = m_cooldowns.constBegin(); it != m_cooldowns.constEnd(); ++it)
		obs_data_set_int(cooldowns, QByteArray::number(it.key()).constData(), it.value());
	obs_data_set_obj(data, "cooldowns", cooldowns);
	obs_data_release(cooldowns);
	const QString path = QDir(m_dataDir).filePath(QStringLiteral("settings.json"));
	obs_data_save_json_safe(data, path.toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

void TexuguitoDock::applyEnabled()
{
	if (m_enabled) {
		if (!m_server->isListening() && !m_server->listen(m_port))
			obs_log(LOG_WARNING, "[texuguito] port %d is in use, overlay not available", m_port);
		m_chattersTimer.start();
		pollChatters();
	} else {
		m_server->close();
		m_chattersTimer.stop();
	}
	m_toggle->setText(m_enabled ? T("Texuguito.TurnOff") : T("Texuguito.TurnOn"));
	refreshStatus();
}

void TexuguitoDock::refreshStatus()
{
	if (!m_enabled) {
		m_status->setText(T("Texuguito.Off"));
	} else if (!m_server->isListening()) {
		m_status->setText(QStringLiteral("<span style=\"color:#E03C3C\">%1</span>")
					  .arg(T("Texuguito.PortInUse").arg(m_port).toHtmlEscaped()));
	} else {
		m_status->setText(T("Texuguito.Status")
					  .arg(overlayUrl().toHtmlEscaped())
					  .arg(m_server->clientCount())
					  .arg(m_engine->clips().size()));
	}

	bool anyChannel = false;
	for (ChatPlatform p : {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick})
		anyChannel |= !m_chat->target(p).trimmed().isEmpty();
	if (m_enabled && !anyChannel)
		m_status->setText(m_status->text() + QStringLiteral("<br><span style=\"color:#E0A000\">%1</span>")
							     .arg(T("Texuguito.NoChannels").toHtmlEscaped()));

	QStringList parts;
	for (ChatPlatform p : ChatAccounts::loginPlatforms()) {
		const bool ok = m_chat->accounts()->account(p).loggedIn() && !m_chat->target(p).trimmed().isEmpty();
		const char *name = p == ChatPlatform::Twitch    ? "Twitch"
				   : p == ChatPlatform::YouTube ? "YouTube"
								: "Kick";
		parts.append(QStringLiteral("%1 %2").arg(QLatin1String(name),
							 ok ? QStringLiteral("✔") : QStringLiteral("✖")));
	}
	m_replies->setText(T("Texuguito.Replies").arg(parts.join(QStringLiteral(" · "))));
}

void TexuguitoDock::onChat(const ChatMessage &msg)
{
	if (!m_enabled)
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = m_recentReplies.begin(); it != m_recentReplies.end();) {
		if (now - it.value() > kEchoWindowMs)
			it = m_recentReplies.erase(it);
		else
			++it;
	}
	if (m_recentReplies.contains(msg.text)) {
		const ChatAccount &a = m_chat->accounts()->account(msg.platform);
		if (!a.login.isEmpty() && a.login.compare(msg.author, Qt::CaseInsensitive) == 0)
			return;
	}

	if (msg.event == ChatEvent::None || msg.event == ChatEvent::Bits) {
		BotMessage bot;
		bot.platform = msg.platform;
		bot.user = msg.author;
		bot.text = msg.text;
		bot.isMod = msg.isMod;
		bot.isSub = msg.isSub;
		bot.isBroadcaster = msg.isBroadcaster;
		bot.isReply = msg.isReply;
		m_engine->handleMessage(bot);
	}
	if (msg.event == ChatEvent::Bits || msg.event == ChatEvent::Donation || msg.event == ChatEvent::Gift)
		m_engine->handleCheer(msg.platform, msg.author);
}

void TexuguitoDock::onReply(ChatPlatform platform, const QString &text)
{
	m_recentReplies.insert(text, QDateTime::currentMSecsSinceEpoch());
	if (!m_chat->sendAs(platform, text))
		obs_log(LOG_INFO, "[texuguito] reply not sent (no login on that platform): %s",
			text.toUtf8().constData());
}

void TexuguitoDock::updateStreamerChannels()
{
	/* YouTube channels are handles or links, not names: the streamer's
	 * avatar shows there after their first message (broadcaster badge). */
	QList<QPair<ChatPlatform, QString>> channels;
	channels.append({ChatPlatform::Twitch, TwitchChat::normalizeChannel(m_chat->target(ChatPlatform::Twitch))});
	const QString kick = KickChat::normalizeChannel(m_chat->target(ChatPlatform::Kick));
	bool numeric = false;
	kick.toLongLong(&numeric);
	if (!numeric) /* a chatroom id, not a name */
		channels.append({ChatPlatform::Kick, kick});
	m_engine->setStreamerChannels(channels);
}

void TexuguitoDock::addAudio()
{
	QDialog dialog(this);
	dialog.setWindowTitle(T("Texuguito.AddAudioTitle"));
	dialog.setMinimumWidth(460);
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(T("Texuguito.AddAudioIntro"), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *form = new QFormLayout();
	auto *linkRow = new QHBoxLayout();
	auto *link = new QLineEdit(&dialog);
	link->setPlaceholderText(QStringLiteral("https://www.myinstants.com/pt/instant/..."));
	auto *pick = new QPushButton(T("Texuguito.AddAudioFile"), &dialog);
	linkRow->addWidget(link, 1);
	linkRow->addWidget(pick);
	form->addRow(T("Texuguito.AddAudioLink"), linkRow);
	auto *price = new QSpinBox(&dialog);
	price->setRange(0, BotEngine::kMaxClipCost);
	const QList<int> costs = m_engine->clipCosts();
	price->setValue(costs.isEmpty() ? 50 : costs.first());
	form->addRow(T("Texuguito.AddAudioPrice"), price);
	auto *name = new QLineEdit(&dialog);
	form->addRow(T("Texuguito.AddAudioName"), name);
	layout->addLayout(form);

	auto *status = new QLabel(&dialog);
	status->setWordWrap(true);
	layout->addWidget(status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);

	connect(pick, &QPushButton::clicked, &dialog, [this, &dialog, link, name]() {
		const QString file = QFileDialog::getOpenFileName(&dialog, T("Texuguito.AddAudioTitle"), QString(),
								  T("Texuguito.AddAudioFileFilter"));
		if (file.isEmpty())
			return;
		link->setText(file);
		if (name->text().trimmed().isEmpty())
			name->setText(SoundFetch::clipName(QFileInfo(file).completeBaseName()));
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	/* Saves and closes, or says why not and stays open. */
	const auto save = [this, &dialog, name, price, status, buttons](const QByteArray &data, const QString &ext,
									const QString &suggested) {
		const QString clip = SoundFetch::clipName(name->text().trimmed().isEmpty() ? suggested : name->text());
		const QString error = m_engine->addClip(data, ext, clip, price->value());
		buttons->setEnabled(true);
		if (!error.isEmpty()) {
			status->setText(error);
			return;
		}
		refreshStatus();
		QMessageBox::information(&dialog, T("Texuguito.AddAudioTitle"),
					 T("Texuguito.Bot.AudioAdded").arg(clip).arg(price->value()));
		dialog.accept();
	};
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, link, status, buttons, save]() {
		const QString source = link->text().trimmed();
		if (source.isEmpty())
			return;
		const QFileInfo local(source);
		if (local.isFile()) {
			QFile file(source);
			if (local.size() > SoundFetch::kMaxAudioBytes || !file.open(QIODevice::ReadOnly)) {
				status->setText(T("Texuguito.Bot.AddAudioTooBig")
							.arg(SoundFetch::kMaxAudioBytes / (1024 * 1024)));
				return;
			}
			const QByteArray data = file.readAll();
			save(data, SoundFetch::audioType(data.left(16)), local.completeBaseName());
			return;
		}
		buttons->setEnabled(false);
		status->setText(T("Texuguito.AddAudioDownloading"));
		SoundFetch::fetch(
			&m_net, source,
			[this, status, buttons, save](const SoundFetch::Result &r) {
				if (r.error != SoundFetch::Error::None) {
					buttons->setEnabled(true);
					status->setText(m_engine->soundFetchError(r));
					return;
				}
				save(r.data, r.ext, r.name);
			},
			&dialog);
	});
	dialog.exec();
}

void TexuguitoDock::pollChatters()
{
	ChatAccounts *accounts = m_chat->accounts();
	const QString channel = m_chat->target(ChatPlatform::Twitch);
	if (!m_enabled || m_chattersDenied || channel.trimmed().isEmpty() ||
	    !accounts->account(ChatPlatform::Twitch).loggedIn())
		return;
	QPointer<TexuguitoDock> self = this;
	accounts->twitchChatters(channel, [self](const QSet<QString> &logins, int status, const QString &error) {
		if (!self)
			return;
		if (status == 200) {
			self->m_engine->setTwitchChatters(logins);
			return;
		}
		/* Not the broadcaster or a mod there: fall back to "who chatted". */
		if (status == 401 || status == 403) {
			self->m_chattersDenied = true;
			obs_log(LOG_INFO, "[texuguito] Twitch viewer list unavailable (%d): %s", status,
				error.toUtf8().constData());
		}
	});
}

void TexuguitoDock::addBrowserSource()
{
	obs_video_info ovi{};
	obs_get_video_info(&ovi);
	const uint32_t height = std::max<uint32_t>(160, ovi.base_height / 5);

	obs_source_t *source = obs_get_source_by_name(kSourceName);
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", overlayUrl().toUtf8().constData());
		obs_data_set_int(settings, "width", ovi.base_width);
		obs_data_set_int(settings, "height", height);
		/* Clips and TTS show up in the OBS mixer like any other source. */
		obs_data_set_bool(settings, "reroute_audio", true);
		source = obs_source_create("browser_source", kSourceName, settings, nullptr);
		obs_data_release(settings);
	}
	if (!source) {
		QMessageBox::information(this, T("Texuguito.Title"), T("Texuguito.NoBrowser").arg(overlayUrl()));
		return;
	}

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene) {
		obs_sceneitem_t *item = obs_scene_add(scene, source);
		vec2 pos{};
		pos.x = 0.0f;
		pos.y = static_cast<float>(ovi.base_height - height);
		obs_sceneitem_set_pos(item, &pos);
	}
	obs_source_release(sceneSource);
	obs_source_release(source);
}

void TexuguitoDock::importOldBot()
{
	const QString dir = QFileDialog::getExistingDirectory(this, T("Texuguito.ImportTitle"));
	if (dir.isEmpty())
		return;
	QMessageBox::information(this, T("Texuguito.ImportTitle"), importFrom(dir));
}

QString TexuguitoDock::importFrom(const QString &dir)
{
	const QDir root(dir);
	int files = 0;
	for (const char *name : {"viewers.json", "points.json", "custom_commands.json"}) {
		const QString src = root.filePath(QStringLiteral("data/") + QLatin1String(name));
		if (!QFile::exists(src))
			continue;
		const QString dst = QDir(m_dataDir).filePath(QLatin1String(name));
		if (QFile::exists(dst)) {
			QFile::remove(dst + QStringLiteral(".bak"));
			QFile::rename(dst, dst + QStringLiteral(".bak"));
		}
		if (QFile::copy(src, dst))
			files++;
	}
	/* The old bot knew its Twitch channel from .env; only that line is read,
	 * never its app or tokens (Twitch logs in with the plugin's own app). */
	QString channel;
	QFile env(root.filePath(QStringLiteral(".env")));
	if (env.open(QIODevice::ReadOnly | QIODevice::Text)) {
		while (!env.atEnd()) {
			const QString line = QString::fromUtf8(env.readLine()).trimmed();
			const qsizetype eq = line.indexOf(QLatin1Char('='));
			if (eq <= 0)
				continue;
			if (line.left(eq).trimmed() != QLatin1String("CHANNEL"))
				continue;
			channel = line.mid(eq + 1).trimmed();
			channel.remove(QLatin1Char('"'));
			channel.remove(QLatin1Char('\''));
		}
	}
	const bool setChannel = !channel.isEmpty() && m_chat->target(ChatPlatform::Twitch).trimmed().isEmpty();
	if (setChannel)
		m_chat->setTarget(ChatPlatform::Twitch, channel);

	const int audios = root.exists(QStringLiteral("audios"))
				   ? copyTree(root.filePath(QStringLiteral("audios")), m_engine->audioDir())
				   : 0;
	m_engine->reloadData();
	refreshStatus();
	obs_log(LOG_INFO, "[texuguito] imported %d data file(s) and %d audio file(s) from %s", files, audios,
		dir.toUtf8().constData());
	QString result = files + audios > 0 ? T("Texuguito.Imported").arg(files).arg(audios)
					    : T("Texuguito.ImportNothing");
	if (setChannel)
		result += QStringLiteral("\n\n") + T("Texuguito.ImportedChannel").arg(channel);
	return result;
}

void TexuguitoDock::openSettings()
{
	QDialog dialog(this);
	dialog.setWindowTitle(T("Texuguito.Title"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout();
	auto *port = new QSpinBox(&dialog);
	port->setRange(1024, 65535);
	port->setValue(m_port);
	form->addRow(T("Texuguito.Port"), port);
	auto *volume = new QSpinBox(&dialog);
	volume->setRange(0, 100);
	volume->setSuffix(QStringLiteral(" %"));
	volume->setValue(static_cast<int>(m_volume * 100.0 + 0.5));
	form->addRow(T("Texuguito.Volume"), volume);
	auto *audioRow = new QHBoxLayout();
	auto *audio = new QLineEdit(m_engine->audioDir(), &dialog);
	auto *browse = new QToolButton(&dialog);
	browse->setText(QStringLiteral("…"));
	connect(browse, &QToolButton::clicked, &dialog, [audio, &dialog]() {
		const QString dir = QFileDialog::getExistingDirectory(&dialog, QString(), audio->text());
		if (!dir.isEmpty())
			audio->setText(dir);
	});
	audioRow->addWidget(audio, 1);
	audioRow->addWidget(browse);
	form->addRow(T("Texuguito.AudioDir"), audioRow);
	layout->addLayout(form);
	auto *note = new QLabel(T("Texuguito.AudioHint"), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);

	/* One wait per price folder that has sounds. */
	QHash<int, QSpinBox *> cooldownSpins;
	const QList<int> costs = m_engine->clipCosts();
	if (!costs.isEmpty()) {
		auto *cooldownForm = new QFormLayout();
		auto *title =
			new QLabel(QStringLiteral("<b>%1</b>").arg(T("Texuguito.Cooldowns").toHtmlEscaped()), &dialog);
		layout->addWidget(title);
		for (int cost : costs) {
			auto *spin = new QSpinBox(&dialog);
			spin->setRange(0, 3600);
			spin->setSuffix(QStringLiteral(" s"));
			spin->setValue(m_engine->clipCooldownSeconds(cost));
			cooldownForm->addRow(T("Texuguito.CooldownPrice").arg(cost), spin);
			cooldownSpins.insert(cost, spin);
		}
		layout->addLayout(cooldownForm);
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	const auto newPort = static_cast<quint16>(port->value());
	m_volume = volume->value() / 100.0;
	m_engine->setVolume(m_volume);
	for (auto it = cooldownSpins.constBegin(); it != cooldownSpins.constEnd(); ++it)
		m_cooldowns.insert(it.key(), it.value()->value());
	m_engine->setClipCooldowns(m_cooldowns);
	if (audio->text().trimmed() != m_engine->audioDir())
		m_engine->setAudioDir(audio->text().trimmed());
	if (newPort != m_port) {
		m_port = newPort;
		m_server->close();
	}
	saveSettings();
	applyEnabled();
}

void texuguito_register(void)
{
	UnifiedChatDock *chat = unifiedChatDock();
	if (!chat) {
		obs_log(LOG_WARNING, "[texuguito] Unified Chat is not available, Texuguito stays off");
		return;
	}
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new TexuguitoDock(chat, main);
	/* Scrolls instead of clipping lines when the dock is short (it sits
	 * under the chat on the Live tab). */
	auto *scroll = new QScrollArea(main);
	scroll->setWidget(dock);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	/* Only scroll up and down: keep the width the buttons need. */
	scroll->setMinimumWidth(dock->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Texuguito.Title"), scroll)) {
		obs_log(LOG_WARNING, "[texuguito] could not add dock");
		delete scroll;
		return;
	}
	g_dock = dock;

	/* The parade sits with the other overlays: a section in the Overlays
	 * panel and a tab in the web panel, on its own server as before. */
	QPointer<TexuguitoDock> self(dock);
	overlaysAddSection({T("Texuguito.Parade.Title"), T("Texuguito.Parade.Help"),
			    [self]() { return self ? self->overlayUrl() : QString(); },
			    [self]() {
				    if (self)
					    self->addBrowserSource();
			    }});
	overlaysAddPanelTab(QStringLiteral("desfile"), {[self]() { return self ? self->panelState() : QJsonObject(); },
							[self](const QJsonObject &panel) {
								return self ? self->applyPanel(panel) : QJsonObject();
							}});

	/* Developer smoke test, inert unless the variable is set: imports the
	 * old bot folder it names, then logs what the dock shows. */
	const QString selftest = qEnvironmentVariable("MEKETREVE_SELFTEST_TEXUGUITO_IMPORT");
	if (!selftest.isEmpty()) {
		QTimer::singleShot(3000, dock, [dock, selftest]() {
			obs_log(LOG_INFO, "[selftest] texuguito status before: %s",
				dock->statusText().toUtf8().constData());
			obs_log(LOG_INFO, "[selftest] texuguito import: %s",
				dock->importFrom(selftest).toUtf8().constData());
			obs_log(LOG_INFO, "[selftest] texuguito status after: %s",
				dock->statusText().toUtf8().constData());
		});
	}
}

void texuguito_unregister(void) {}
