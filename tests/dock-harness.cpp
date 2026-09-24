/*
Meketreve OBS Essentials - dock-harness (developer tool)
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

/* Shows one of the plugin's Qt widgets outside OBS and saves a screenshot:
 *   QT_QPA_PLATFORM=offscreen dock-harness chat out.png 20 --locale pt-BR --config <dir>
 * The widget reads its config from <dir> (default: a "harness-config"
 * directory next to the binary), so a unified-chat.json there picks the
 * channels. */

#include "unified-chat-dock.hpp"
#include "config-share.hpp"
#include "outputs-dock.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include "../src/tools/unified-chat.h"

#include <obs-module.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/text-lookup.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QDir>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>

#include <cstdio>

namespace {

lookup_t *g_lookup = nullptr;
QByteArray g_configDir;

} // namespace

/* The three module entry points the plugin code expects from OBS. */
extern "C" {

const char *obs_module_text(const char *val)
{
	const char *out = val;
	if (g_lookup)
		text_lookup_getstr(g_lookup, val, &out);
	return out;
}

obs_module_t *obs_current_module(void)
{
	return nullptr;
}

char *obs_module_get_config_path(obs_module_t *, const char *file)
{
	struct dstr path = {};
	dstr_copy(&path, g_configDir.constData());
	dstr_cat(&path, "/");
	dstr_cat(&path, file);
	return path.array;
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QStringList args = app.arguments();

	QString locale = QStringLiteral("en-US");
	QString configDir = QCoreApplication::applicationDirPath() + QStringLiteral("/harness-config");
	for (qsizetype i = 1; i + 1 < args.size(); i++) {
		if (args[i] == QLatin1String("--locale")) {
			locale = args[i + 1];
			args.remove(i, 2);
			i--;
		} else if (args[i] == QLatin1String("--config")) {
			configDir = args[i + 1];
			args.remove(i, 2);
			i--;
		}
	}
	if (args.size() < 3) {
		std::fprintf(
			stderr,
			"usage: dock-harness <chat|chat-settings|activity|outputs|export|import> <out.png> [seconds] [--locale xx-XX] [--config dir]\n");
		return 2;
	}

	QDir().mkpath(configDir);
	g_configDir = configDir.toUtf8();
	g_lookup =
		text_lookup_create(QStringLiteral(HARNESS_DATA_DIR "/locale/%1.ini").arg(locale).toUtf8().constData());
	if (!g_lookup)
		std::fprintf(stderr, "warning: locale %s not found, showing keys\n", qPrintable(locale));

	QWidget *widget = nullptr;
	UnifiedChatDock *chat = nullptr;
	const QString out = args[2];
	const int seconds = args.size() > 3 ? args[3].toInt() : 3;

	if (args[1] == QLatin1String("export") || args[1] == QLatin1String("import")) {
		/* The dialogs are modal: grab whatever window is active once they
		 * are up, then close it. */
		chat = new UnifiedChatDock();
		configShareAddSection({QStringLiteral("chat"), "Config.Section.Chat",
				       [chat]() { return QJsonValue(chat->exportChannels()); },
				       [chat](const QJsonValue &v) { chat->importChannels(v.toObject()); },
				       [](const QJsonValue &v) {
					       return UnifiedChatDock::describeChannels(v.toObject());
				       }});
		QTimer::singleShot(seconds * 1000, &app, [&out]() {
			QWidget *w = QApplication::activeModalWidget();
			const bool ok = w && w->grab().save(out);
			std::printf("%s %s\n", ok ? "saved" : "FAILED to save", qPrintable(out));
			if (w)
				w->close();
		});
		if (args[1] == QLatin1String("export"))
			configShareOpenExport();
		else
			configShareOpenImport();
		delete chat;
		text_lookup_destroy(g_lookup);
		return 0;
	}

	if (args[1] == QLatin1String("chat-settings")) {
		chat = new UnifiedChatDock();
		QTimer::singleShot(seconds * 1000, &app, [&out]() {
			QWidget *w = QApplication::activeModalWidget();
			const bool ok = w && w->grab().save(out);
			std::printf("%s %s\n", ok ? "saved" : "FAILED to save", qPrintable(out));
			if (w)
				w->close();
		});
		for (QToolButton *b : chat->findChildren<QToolButton *>()) {
			if (b->text() == QString::fromUtf8(obs_module_text("UnifiedChat.Settings")))
				b->click();
		}
		delete chat;
		text_lookup_destroy(g_lookup);
		return 0;
	}

	if (args[1] == QLatin1String("chat")) {
		chat = new UnifiedChatDock();
		widget = chat;
	} else if (args[1] == QLatin1String("activity")) {
		/* Chat and Activity side by side, fed with one event of each kind. */
		auto *box = new QWidget();
		auto *h = new QHBoxLayout(box);
		chat = new UnifiedChatDock(box);
		auto *activity = new ActivityDock(box);
		QObject::connect(chat, &UnifiedChatDock::activity, activity, &ActivityDock::addEvent);
		h->addWidget(chat);
		h->addWidget(activity);
		const auto ev = [chat](ChatPlatform p, ChatEvent e, const char *who, int amount, const char *detail,
				       const char *text) {
			ChatMessage m{p, QString::fromUtf8(who), QString(), QString::fromUtf8(text), QString()};
			m.event = e;
			m.amount = amount;
			m.detail = QString::fromUtf8(detail);
			chat->appendMessage(m);
		};
		chat->appendMessage({ChatPlatform::Twitch, QStringLiteral("viewer1"), QStringLiteral("#1E90FF"),
				     QStringLiteral("boa noite!"), QString()});
		ev(ChatPlatform::Twitch, ChatEvent::Sub, "Resubber", 5, "Tier 1", "five months!");
		ev(ChatPlatform::Twitch, ChatEvent::GiftSub, "Santa", 20, "Tier 1", "");
		ev(ChatPlatform::Twitch, ChatEvent::Bits, "Cheerer", 100, "", "Cheer100 gg");
		ev(ChatPlatform::Kick, ChatEvent::Raid, "Raider", 1234, "", "");
		ev(ChatPlatform::YouTube, ChatEvent::Donation, "Rich", 0, "R$ 10,00", "valeu pela live");
		ev(ChatPlatform::YouTube, ChatEvent::Membership, "Member", 0, "Welcome to Members!", "");
		ev(ChatPlatform::TikTok, ChatEvent::Gift, "Fan", 7, "Rose (7 \u2666)", "");
		ev(ChatPlatform::TikTok, ChatEvent::Follow, "newfan", 0, "", "");
		ev(ChatPlatform::TikTok, ChatEvent::Share, "sharer", 0, "", "");
		ev(ChatPlatform::Kick, ChatEvent::Follow, "kickfan", 0, "", "");
		chat->appendMessage(
			{ChatPlatform::Kick, QStringLiteral("kicker"), QString(), QStringLiteral("salve"), QString()});
		widget = box;
		box->resize(900, 560);
	} else if (args[1] == QLatin1String("outputs")) {
		auto *outputs = new OutputsDock();
		outputs->importOutputs(QJsonDocument::fromJson(R"json([
			{"name":"Twitch","platform":"twitch","server":"rtmp://live.twitch.tv/app"},
			{"name":"YouTube","platform":"youtube","server":"rtmps://a.rtmps.youtube.com:443/live2"},
			{"name":"Kick (encoder próprio)","platform":"kick","server":"rtmps://x/app","sharedEncoder":false}
		])json")
					       .array());
		widget = outputs;
	} else {
		std::fprintf(stderr, "unknown widget '%s'\n", qPrintable(args[1]));
		return 2;
	}

	if (widget->size().width() < 600)
		widget->resize(420, 560);
	widget->show();

	QTimer::singleShot(seconds * 1000, &app, [&]() {
		const bool ok = widget->grab().save(out);
		std::printf("%s %s\n", ok ? "saved" : "FAILED to save", qPrintable(out));
		if (chat)
			chat->shutdown();
		app.exit(ok ? 0 : 1);
	});
	const int rc = app.exec();
	delete widget;
	text_lookup_destroy(g_lookup);
	return rc;
}
