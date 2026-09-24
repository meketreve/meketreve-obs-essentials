/*
Meketreve OBS Essentials - chat-probe (developer tool)
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

/* Runs one chat connector outside OBS and prints what it sees:
 *   chat-probe twitch xqc 25
 * Exit code 0 when at least one message arrived, 1 otherwise. */

#include "chat-accounts.hpp"
#include "tts-client.hpp"
#include "kick-chat.hpp"
#include "tiktok-chat.hpp"
#include "twitch-chat.hpp"
#include "youtube-chat.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

const char *stateName(ConnectorState s)
{
	switch (s) {
	case ConnectorState::Idle:
		return "idle";
	case ConnectorState::Connecting:
		return "connecting";
	case ConnectorState::Connected:
		return "connected";
	case ConnectorState::Offline:
		return "offline";
	case ConnectorState::Error:
		return "error";
	}
	return "?";
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 3) {
		std::fprintf(stderr, "usage: chat-probe <twitch|youtube|kick|tiktok> <channel> [seconds]\n");
		return 2;
	}

	QNetworkAccessManager net;
	const QString platform = args[1].toLower();

	/* chat-probe tts "<text>" <out.mp3>: Google TTS as the Texuguito uses it. */
	if (platform == QLatin1String("tts") && args.size() > 3) {
		int rc = 1;
		GoogleTts::synthesize(
			&net, args[2],
			[&](QByteArray mp3, QString error) {
				QFile out(args[3]);
				if (!mp3.isEmpty() && out.open(QIODevice::WriteOnly))
					out.write(mp3);
				std::printf("[tts] %lld bytes %s\n", static_cast<long long>(mp3.size()),
					    qPrintable(error));
				rc = mp3.isEmpty() ? 1 : 0;
				app.quit();
			},
			&app);
		app.exec();
		return rc;
	}

	/* chat-probe login-twitch <client id> | login-kick <client id> <secret>:
	 * runs the login flow; for Kick the browser step is simulated by
	 * calling the local callback with a made-up code. */
	if (platform.startsWith(QLatin1String("login-"))) {
		const ChatPlatform p = platform == QLatin1String("login-kick") ? ChatPlatform::Kick
									       : ChatPlatform::Twitch;
		ChatAccounts accounts(QDir::tempPath() + QStringLiteral("/chat-probe-accounts.json"));
		accounts.setClient(p, args[2], args.size() > 3 ? args[3] : QString());
		QTextStream out(stdout);
		QObject::connect(&accounts, &ChatAccounts::deviceCode, [&out](const QString &code, const QUrl &url) {
			out << "[device] open " << url.toString() << " code " << code << Qt::endl;
		});
		QObject::connect(&accounts, &ChatAccounts::openBrowser, [&out, &net](const QUrl &url) {
			out << "[browser] " << url.toString() << Qt::endl;
			const QString state = QUrlQuery(url).queryItemValue(QStringLiteral("state"));
			net.get(QNetworkRequest(QUrl(ChatAccounts::kickRedirectUri() +
						     QStringLiteral("?code=fake-code&state=") + state)));
		});
		QObject::connect(&accounts, &ChatAccounts::loginFailed, [&out, &app](ChatPlatform, const QString &e) {
			out << "[login failed] " << e << Qt::endl;
			app.exit(3);
		});
		QObject::connect(&accounts, &ChatAccounts::accountChanged, [&out, &accounts, &app, p](ChatPlatform) {
			if (accounts.account(p).loggedIn()) {
				out << "[logged in] " << accounts.account(p).login << Qt::endl;
				app.exit(0);
			}
		});
		QTimer::singleShot(args.size() > 4 ? args[4].toInt() * 1000 : 20000, &app, [&app]() { app.exit(4); });
		accounts.logIn(p);
		return app.exec();
	}
	ChatConnector *c = nullptr;
	if (platform == QLatin1String("twitch"))
		c = new TwitchChat(&net, &app);
	else if (platform == QLatin1String("youtube"))
		c = new YouTubeChat(&net, &app);
	else if (platform == QLatin1String("kick"))
		c = new KickChat(&net, &app);
	else if (platform == QLatin1String("tiktok"))
		c = new TikTokChat(&net, &app);
	if (!c) {
		std::fprintf(stderr, "unknown platform '%s'\n", qPrintable(platform));
		return 2;
	}

	QTextStream out(stdout);
	int messages = 0;
	QObject::connect(c, &ChatConnector::stateChanged, [&out](ConnectorState s, const QString &detail) {
		out << "[state] " << stateName(s)
		    << (detail.isEmpty() ? QString() : QStringLiteral(" (") + detail + ')') << Qt::endl;
	});
	QObject::connect(c, &ChatConnector::messageReceived, [&out, &messages](const ChatMessage &m) {
		messages++;
		if (m.event != ChatEvent::None) {
			out << "[event " << static_cast<int>(m.event) << "] " << m.author << " amount=" << m.amount
			    << " detail=" << m.detail << " text=" << m.text << Qt::endl;
			return;
		}
		out << "[msg] ";
		if (!m.highlight.isEmpty())
			out << '[' << m.highlight << "] ";
		out << m.author << ": " << m.text << Qt::endl;
	});

	const int seconds = args.size() > 3 ? args[3].toInt() : 20;
	QTimer::singleShot(seconds * 1000, &app, [&]() {
		c->stop();
		out << "[done] " << messages << " message(s) in " << seconds << "s" << Qt::endl;
		app.exit(messages > 0 ? 0 : 1);
	});
	c->start(args[2]);
	return app.exec();
}
