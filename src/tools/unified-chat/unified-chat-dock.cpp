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

#include "unified-chat-dock.hpp"

#include "kick-chat.hpp"
#include "tiktok-chat.hpp"
#include "twitch-chat.hpp"
#include "youtube-chat.hpp"

#include "../unified-chat.h"
#include "../config/config-share.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr const char *kDockId = "meketreve-unified-chat";
constexpr const char *kConfigFile = "unified-chat.json";
constexpr int kMaxLines = 500;

struct PlatformInfo {
	const char *configKey;
	const char *labelKey;
	const char *placeholderKey;
	const char *tag;
	const char *tagBackground;
	const char *tagForeground;
};

const std::array<PlatformInfo, 4> kPlatformInfo{{
	{"twitch", "UnifiedChat.Twitch", "UnifiedChat.Twitch.Placeholder", "TW", "#9146FF", "#FFFFFF"},
	{"youtube", "UnifiedChat.YouTube", "UnifiedChat.YouTube.Placeholder", "YT", "#FF0033", "#FFFFFF"},
	{"kick", "UnifiedChat.Kick", "UnifiedChat.Kick.Placeholder", "KK", "#53FC18", "#000000"},
	{"tiktok", "UnifiedChat.TikTok", "UnifiedChat.TikTok.Placeholder", "TT", "#FE2C55", "#FFFFFF"},
}};

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

size_t indexOf(ChatPlatform p)
{
	return static_cast<size_t>(p);
}

QString configPath()
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *file = obs_module_config_path(kConfigFile);
	const QString path = QString::fromUtf8(file ? file : "");
	bfree(file);
	return path;
}

/* Twitch/Kick let users pick any name color, including ones that vanish on
 * the dock background; nudge those toward readable. Users without a color
 * get a stable one derived from their name. */
QString readableColor(const QString &requested, const QString &author, const QColor &background)
{
	QColor c(requested);
	if (!c.isValid()) {
		const int hue = static_cast<int>(qHash(author) % 360);
		c = QColor::fromHsl(hue, 170, 150);
	}
	/* Perceived brightness, not HSL lightness: pure blue has HSL lightness
	 * 50% yet is nearly invisible on a dark background. */
	const auto luma = [](const QColor &k) {
		return 0.299 * k.red() + 0.587 * k.green() + 0.114 * k.blue();
	};
	const bool darkBg = luma(background) < 128;
	const int hue = std::max(c.hslHue(), 0);
	for (int l = c.lightness(); darkBg && luma(c) < 120 && l < 255; l += 8)
		c = QColor::fromHsl(hue, c.hslSaturation(), std::min(l + 8, 255));
	for (int l = c.lightness(); !darkBg && luma(c) > 150 && l > 0; l -= 8)
		c = QColor::fromHsl(hue, c.hslSaturation(), std::max(l - 8, 0));
	return c.name();
}

QPointer<UnifiedChatDock> g_dock;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT && g_dock)
		g_dock->shutdown();
}

} // namespace

UnifiedChatDock::UnifiedChatDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto *bar = new QHBoxLayout();
	bar->setSpacing(8);
	for (size_t i = 0; i < kPlatforms; i++) {
		m_status[i] = new QLabel(this);
		bar->addWidget(m_status[i]);
	}
	bar->addStretch();

	auto *clear = new QToolButton(this);
	clear->setText(T("UnifiedChat.Clear"));
	connect(clear, &QToolButton::clicked, this, [this]() {
		m_view->clear();
		m_hasMessages = false;
		showPlaceholder();
	});
	bar->addWidget(clear);

	auto *settings = new QToolButton(this);
	settings->setText(T("UnifiedChat.Settings"));
	connect(settings, &QToolButton::clicked, this, &UnifiedChatDock::openSettings);
	bar->addWidget(settings);
	layout->addLayout(bar);

	m_view = new QTextBrowser(this);
	m_view->setOpenLinks(false);
	m_view->document()->setMaximumBlockCount(kMaxLines);
	layout->addWidget(m_view);

	m_connectors[indexOf(ChatPlatform::Twitch)] = new TwitchChat(&m_net, this);
	m_connectors[indexOf(ChatPlatform::YouTube)] = new YouTubeChat(&m_net, this);
	m_connectors[indexOf(ChatPlatform::Kick)] = new KickChat(&m_net, this);
	m_connectors[indexOf(ChatPlatform::TikTok)] = new TikTokChat(&m_net, this);

	for (ChatConnector *c : m_connectors) {
		connect(c, &ChatConnector::messageReceived, this, &UnifiedChatDock::appendMessage);
		const ChatPlatform platform = c->platform();
		connect(c, &ChatConnector::stateChanged, this,
			[this, platform](ConnectorState state, const QString &detail) {
				updateStatus(platform, state, detail);
			});
		updateStatus(platform, ConnectorState::Idle, QString());
	}

	loadSettings();
	showPlaceholder();
	applySettings();
}

UnifiedChatDock::~UnifiedChatDock()
{
	/* The connectors are destroyed after the status labels; a socket that
	 * reports "closed" on the way out must not reach them. */
	for (ChatConnector *c : m_connectors)
		disconnect(c, nullptr, this, nullptr);
	shutdown();
}

void UnifiedChatDock::shutdown()
{
	for (ChatConnector *c : m_connectors)
		c->stop();
}

QJsonObject UnifiedChatDock::exportChannels() const
{
	QJsonObject o;
	for (size_t i = 0; i < kPlatforms; i++)
		o.insert(QString::fromLatin1(kPlatformInfo[i].configKey), m_targets[i]);
	return o;
}

void UnifiedChatDock::importChannels(const QJsonObject &channels)
{
	for (size_t i = 0; i < kPlatforms; i++)
		m_targets[i] = channels.value(QString::fromLatin1(kPlatformInfo[i].configKey)).toString().trimmed();
	saveSettings();
	applySettings();
	if (!m_hasMessages)
		showPlaceholder();
}

QString UnifiedChatDock::describeChannels(const QJsonObject &channels)
{
	QStringList parts;
	for (const PlatformInfo &info : kPlatformInfo) {
		const QString target = channels.value(QString::fromLatin1(info.configKey)).toString().trimmed();
		if (!target.isEmpty())
			parts.append(QStringLiteral("%1: %2").arg(T(info.labelKey), target));
	}
	return parts.isEmpty() ? T("Config.ChatNone") : parts.join(QStringLiteral(", "));
}

void UnifiedChatDock::loadSettings()
{
	obs_data_t *data = obs_data_create_from_json_file_safe(configPath().toUtf8().constData(), "bak");
	if (!data)
		return;
	for (size_t i = 0; i < kPlatforms; i++)
		m_targets[i] = QString::fromUtf8(obs_data_get_string(data, kPlatformInfo[i].configKey));
	obs_data_release(data);
}

void UnifiedChatDock::saveSettings()
{
	obs_data_t *data = obs_data_create();
	for (size_t i = 0; i < kPlatforms; i++)
		obs_data_set_string(data, kPlatformInfo[i].configKey, m_targets[i].toUtf8().constData());
	if (!obs_data_save_json_safe(data, configPath().toUtf8().constData(), "tmp", "bak"))
		obs_log(LOG_WARNING, "[unified-chat] could not save %s", kConfigFile);
	obs_data_release(data);
}

void UnifiedChatDock::applySettings()
{
	for (size_t i = 0; i < kPlatforms; i++) {
		if (m_targets[i].trimmed().isEmpty())
			m_connectors[i]->stop();
		else
			m_connectors[i]->start(m_targets[i]);
	}
}

void UnifiedChatDock::openSettings()
{
	QDialog dialog(this);
	dialog.setWindowTitle(T("UnifiedChat.SettingsTitle"));
	dialog.setMinimumWidth(420);

	auto *layout = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout();
	std::array<QLineEdit *, kPlatforms> edits{};
	for (size_t i = 0; i < kPlatforms; i++) {
		edits[i] = new QLineEdit(m_targets[i], &dialog);
		edits[i]->setPlaceholderText(T(kPlatformInfo[i].placeholderKey));
		edits[i]->setClearButtonEnabled(true);
		form->addRow(T(kPlatformInfo[i].labelKey), edits[i]);
	}
	layout->addLayout(form);

	auto *hint = new QLabel(T("UnifiedChat.Hint"), &dialog);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	auto *tiktokNote = new QLabel(T("UnifiedChat.TikTok.Note"), &dialog);
	tiktokNote->setWordWrap(true);
	QFont small = tiktokNote->font();
	small.setPointSizeF(small.pointSizeF() * 0.9);
	tiktokNote->setFont(small);
	layout->addWidget(tiktokNote);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return;

	bool changed = false;
	for (size_t i = 0; i < kPlatforms; i++) {
		const QString value = edits[i]->text().trimmed();
		if (value != m_targets[i]) {
			m_targets[i] = value;
			changed = true;
		}
	}
	if (!changed)
		return;

	saveSettings();
	applySettings();
	if (!m_hasMessages)
		showPlaceholder();
}

void UnifiedChatDock::showPlaceholder()
{
	const bool anyConfigured = std::any_of(m_targets.begin(), m_targets.end(),
					       [](const QString &t) { return !t.trimmed().isEmpty(); });
	m_view->setHtml(QStringLiteral("<p style=\"color:gray\">%1</p>")
				.arg(T(anyConfigured ? "UnifiedChat.Waiting" : "UnifiedChat.Empty").toHtmlEscaped()));
}

void UnifiedChatDock::appendMessage(const ChatMessage &msg)
{
	if (!m_hasMessages) {
		m_view->clear();
		m_hasMessages = true;
	}

	QScrollBar *scroll = m_view->verticalScrollBar();
	const bool atBottom = scroll->value() >= scroll->maximum() - 4;

	const PlatformInfo &info = kPlatformInfo[indexOf(msg.platform)];
	const QColor background = m_view->palette().color(QPalette::Base);

	QString html =
		QStringLiteral("<span style=\"color:gray\">%1</span> "
			       "<span style=\"background-color:%2;color:%3;font-weight:bold\">&nbsp;%4&nbsp;</span> ")
			.arg(QTime::currentTime().toString(QStringLiteral("HH:mm")), QLatin1String(info.tagBackground),
			     QLatin1String(info.tagForeground), QLatin1String(info.tag));
	if (!msg.highlight.isEmpty())
		html += QStringLiteral("<span style=\"color:#FFB300;font-weight:bold\">[%1]</span> ")
				.arg(msg.highlight.toHtmlEscaped());
	html += QStringLiteral("<b style=\"color:%1\">%2</b>: %3")
			.arg(readableColor(msg.authorColor, msg.author, background), msg.author.toHtmlEscaped(),
			     msg.text.toHtmlEscaped());

	m_view->append(html);
	if (atBottom)
		scroll->setValue(scroll->maximum());
}

void UnifiedChatDock::updateStatus(ChatPlatform platform, ConnectorState state, const QString &detail)
{
	const size_t i = indexOf(platform);
	const char *color = "gray";
	const char *stateKey = "UnifiedChat.State.Idle";
	switch (state) {
	case ConnectorState::Idle:
		break;
	case ConnectorState::Connecting:
		color = "#E0A000";
		stateKey = "UnifiedChat.State.Connecting";
		break;
	case ConnectorState::Connected:
		color = "#2EB82E";
		stateKey = "UnifiedChat.State.Connected";
		break;
	case ConnectorState::Offline:
		color = "#7A7A7A";
		stateKey = "UnifiedChat.State.Offline";
		break;
	case ConnectorState::Error:
		color = "#E03C3C";
		stateKey = "UnifiedChat.State.Error";
		break;
	}

	QLabel *label = m_status[i];
	label->setText(QStringLiteral("<span style=\"color:%1\">&#9679;</span> %2")
			       .arg(QLatin1String(color), QLatin1String(kPlatformInfo[i].tag)));
	QString tip = T(kPlatformInfo[i].labelKey) + QStringLiteral(": ") + T(stateKey);
	if (!detail.isEmpty())
		tip += QStringLiteral("\n") + detail;
	label->setToolTip(tip);
	label->setVisible(state != ConnectorState::Idle);
}

void unified_chat_register(void)
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new UnifiedChatDock(main);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("UnifiedChat.Title"), dock)) {
		obs_log(LOG_WARNING, "[unified-chat] could not add dock");
		delete dock;
		return;
	}
	g_dock = dock;
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	configShareAddSection({QStringLiteral("chat"), "Config.Section.Chat",
			       []() { return g_dock ? QJsonValue(g_dock->exportChannels()) : QJsonValue(); },
			       [](const QJsonValue &v) {
				       if (g_dock)
					       g_dock->importChannels(v.toObject());
			       },
			       [](const QJsonValue &v) {
				       return UnifiedChatDock::describeChannels(v.toObject());
			       }});
}

void unified_chat_unregister(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
}
