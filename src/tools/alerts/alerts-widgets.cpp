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

/* The overlays the events and the chat fill (goals, poll, subathon), apart from
 * alerts-dock.cpp: they share its server, token and events. */

#include "alerts-dock.hpp"

#include "../unified-chat/unified-chat-dock.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString webFile(const char *name)
{
	char *dir = obs_module_file("alerts/web");
	const QString path = QDir(QString::fromUtf8(dir ? dir : "")).filePath(QString::fromLatin1(name));
	bfree(dir);
	return path;
}

QString language()
{
	const char *locale = obs_get_locale();
	return locale && QByteArray(locale).startsWith("pt") ? QStringLiteral("pt") : QStringLiteral("en");
}

void jsonReply(OverlayServer::Reply &reply, int status, const QJsonObject &body)
{
	reply.status = status;
	reply.type = "application/json";
	reply.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void errorReply(OverlayServer::Reply &reply, int status, const QString &error)
{
	jsonReply(reply, status, QJsonObject{{QStringLiteral("error"), error}});
}

QJsonObject readJson(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

bool writeJson(const QString &path, const QJsonObject &object)
{
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly) &&
	       file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0 && file.commit();
}

qint64 now()
{
	return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

void AlertsDock::loadWidgets()
{
	m_goals = Goals::normalize(readJson(QDir(m_dir).filePath(QStringLiteral("goals.json"))), T);

	const QJsonObject poll = readJson(QDir(m_dir).filePath(QStringLiteral("poll.json")));
	m_pollConfig = Poll::normalizeConfig(poll.value(QStringLiteral("config")).toObject());
	m_poll.load(poll.value(QStringLiteral("poll")).toObject());
	m_pollTimer = new QTimer(this);
	m_pollTimer->setInterval(250);
	connect(m_pollTimer, &QTimer::timeout, this, &AlertsDock::pollTick);
	/* Open when OBS closed: it goes on, or closes now if its time is up. */
	if (m_poll.isOpen())
		m_pollTimer->start();

	/* The end is a time of day: a running subathon kept running meanwhile. */
	const QJsonObject subathon = readJson(QDir(m_dir).filePath(QStringLiteral("subathon.json")));
	m_subathonConfig = Subathon::normalizeConfig(subathon.value(QStringLiteral("config")).toObject());
	m_subathon.load(subathon.value(QStringLiteral("timer")).toObject());
}

void AlertsDock::saveSubathon()
{
	const QJsonObject stored{{QStringLiteral("config"), m_subathonConfig},
				 {QStringLiteral("timer"), m_subathon.save()}};
	if (!writeJson(QDir(m_dir).filePath(QStringLiteral("subathon.json")), stored))
		obs_log(LOG_WARNING, "[alerts] could not save subathon.json");
}

QJsonObject AlertsDock::subathonMessage(qint64 added) const
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("subathon")},
			   {QStringLiteral("config"), m_subathonConfig},
			   {QStringLiteral("timer"), m_subathon.toJson(now())},
			   {QStringLiteral("added"), static_cast<double>(added)},
			   {QStringLiteral("texts"),
			    QJsonObject{{QStringLiteral("ended"), T("Subathon.Overlay.Ended")},
					{QStringLiteral("paused"), T("Subathon.Overlay.Paused")}}}};
}

void AlertsDock::broadcastSubathon(qint64 added)
{
	m_server->broadcast(subathonMessage(added));
}

void AlertsDock::saveGoals()
{
	if (!writeJson(QDir(m_dir).filePath(QStringLiteral("goals.json")), m_goals))
		obs_log(LOG_WARNING, "[alerts] could not save goals.json");
}

QJsonObject AlertsDock::goalsMessage() const
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("goals")},
			   {QStringLiteral("config"), m_goals},
			   {QStringLiteral("lang"), language()}};
}

void AlertsDock::broadcastGoals()
{
	m_server->broadcast(goalsMessage());
}

void AlertsDock::widgetsEvent(const Alerts::Event &event)
{
	if (Goals::apply(m_goals, event)) {
		saveGoals();
		broadcastGoals();
	}
	const qint64 seconds = Subathon::secondsFor(m_subathonConfig, event);
	if (seconds > 0 && m_subathon.add(seconds, now())) {
		saveSubathon();
		broadcastSubathon(seconds);
	}
}

void AlertsDock::widgetsChat(const ChatMessage &msg)
{
	if (msg.event != ChatEvent::None || !m_poll.isOpen())
		return;
	const int option = Poll::voteFromChat(msg.text);
	if (option == 0)
		return;
	const QString user = msg.userId.isEmpty() ? msg.author.toLower() : msg.userId;
	if (m_poll.vote(Alerts::platformKey(msg.platform) + QLatin1Char(':') + user, option, now()))
		m_pollDirty = true;
}

void AlertsDock::savePoll()
{
	const QJsonObject stored{{QStringLiteral("config"), m_pollConfig}, {QStringLiteral("poll"), m_poll.save()}};
	if (!writeJson(QDir(m_dir).filePath(QStringLiteral("poll.json")), stored))
		obs_log(LOG_WARNING, "[alerts] could not save poll.json");
}

QJsonObject AlertsDock::pollMessage() const
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("poll")},
			   {QStringLiteral("config"), m_pollConfig},
			   {QStringLiteral("poll"), m_poll.toJson(now())},
			   /* The overlay gets its few strings here, in the OBS language. */
			   {QStringLiteral("texts"),
			    QJsonObject{{QStringLiteral("vote"), T("Poll.Overlay.Vote")},
					{QStringLiteral("result"), T("Poll.Overlay.Result")},
					{QStringLiteral("noVotes"), T("Poll.Overlay.NoVotes")}}},
			   {QStringLiteral("lang"), language()}};
}

void AlertsDock::broadcastPoll()
{
	m_server->broadcast(pollMessage());
}

void AlertsDock::pollTick()
{
	if (m_poll.expire(now())) {
		pollClosed();
	} else if (m_pollDirty) {
		m_pollDirty = false;
		savePoll();
		broadcastPoll();
	}
}

void AlertsDock::pollClosed()
{
	m_pollTimer->stop();
	m_pollDirty = false;
	savePoll();
	broadcastPoll();
	const int winner = m_poll.winner();
	if (winner > 0)
		announce(T("Poll.Announce.Result")
				 .arg(m_poll.question(), m_poll.options().at(winner - 1))
				 .arg(m_poll.counts().at(winner - 1)));
	else
		announce(T("Poll.Announce.NoVotes").arg(m_poll.question()));
}

void AlertsDock::announce(const QString &text)
{
	if (!m_pollConfig.value(QStringLiteral("announce")).toBool())
		return;
	for (const ChatPlatform platform : {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick})
		m_chat->sendAs(platform, text);
}

void AlertsDock::widgetsLiveStarted()
{
	if (Goals::resetForLive(m_goals)) {
		saveGoals();
		broadcastGoals();
	}
}

void AlertsDock::widgetsSnapshot(QJsonObject &snapshot) const
{
	snapshot.insert(QStringLiteral("goals"), goalsMessage());
	snapshot.insert(QStringLiteral("poll"), pollMessage());
	snapshot.insert(QStringLiteral("subathon"), subathonMessage());
}

bool AlertsDock::widgetsPage(const QString &path, OverlayServer::Reply &reply) const
{
	if (path == QLatin1String("/metas"))
		reply.file = webFile("goals.html");
	else if (path == QLatin1String("/metas-editor"))
		reply.file = webFile("goals-editor.html");
	else if (path == QLatin1String("/enquete"))
		reply.file = webFile("poll.html");
	else if (path == QLatin1String("/enquete-editor"))
		reply.file = webFile("poll-editor.html");
	else if (path == QLatin1String("/subathon"))
		reply.file = webFile("subathon.html");
	else if (path == QLatin1String("/subathon-editor"))
		reply.file = webFile("subathon-editor.html");
	else
		return false;
	return true;
}

bool AlertsDock::widgetsApi(const OverlayServer::Request &request, OverlayServer::Reply &reply)
{
	const QString &path = request.path;
	const bool get = request.method == "GET";
	const bool post = request.method == "POST";
	const QJsonDocument doc = post ? QJsonDocument::fromJson(request.body) : QJsonDocument();
	/* An empty body is {}: stop and clear need nothing. */
	if (post && !request.body.trimmed().isEmpty() && !doc.isObject() &&
	    (path.startsWith(QLatin1String("/api/goals")) || path.startsWith(QLatin1String("/api/poll")) ||
	     path.startsWith(QLatin1String("/api/subathon")))) {
		errorReply(reply, 400, QStringLiteral("bad json"));
		return true;
	}
	const QJsonObject body = doc.object();
	if (get && path == QLatin1String("/api/goals")) {
		jsonReply(reply, 200, m_goals);
	} else if (post && path == QLatin1String("/api/goals")) {
		m_goals = Goals::normalize(body, T, m_goals);
		saveGoals();
		broadcastGoals();
		jsonReply(reply, 200, m_goals);
	} else if (post && path == QLatin1String("/api/goals-adjust")) {
		/* {id, value, set}: "+1" and "-1" add, "Reset" sets 0. */
		if (!Goals::adjust(m_goals, body.value(QStringLiteral("id")).toString(),
				   body.value(QStringLiteral("value")).toDouble(),
				   body.value(QStringLiteral("set")).toBool())) {
			errorReply(reply, 404, QStringLiteral("not found"));
			return true;
		}
		saveGoals();
		broadcastGoals();
		jsonReply(reply, 200, m_goals);
	} else if (get && path == QLatin1String("/api/poll")) {
		jsonReply(reply, 200, pollMessage());
	} else if (post && path == QLatin1String("/api/poll-config")) {
		m_pollConfig = Poll::normalizeConfig(body);
		savePoll();
		broadcastPoll();
		jsonReply(reply, 200, pollMessage());
	} else if (post && path == QLatin1String("/api/poll-start")) {
		/* {question, options: [...], seconds}; a poll still open is replaced. */
		QStringList options;
		for (const QJsonValue v : body.value(QStringLiteral("options")).toArray())
			options.append(v.toString());
		const QString error = m_poll.start(body.value(QStringLiteral("question")).toString(), options,
						   body.value(QStringLiteral("seconds")).toInt(), now());
		if (!error.isEmpty()) {
			errorReply(reply, 400, error);
			return true;
		}
		m_pollDirty = false;
		m_pollTimer->start();
		savePoll();
		broadcastPoll();
		QStringList numbered;
		for (int i = 0; i < m_poll.options().size(); i++)
			numbered.append(QStringLiteral("%1) %2").arg(i + 1).arg(m_poll.options().at(i)));
		announce(T("Poll.Announce.Start").arg(m_poll.question(), numbered.join(QStringLiteral(" · "))));
		jsonReply(reply, 200, pollMessage());
	} else if (post && path == QLatin1String("/api/poll-stop")) {
		/* Closing by hand goes the same way as the time running out. */
		if (m_poll.stop(now()))
			pollClosed();
		jsonReply(reply, 200, pollMessage());
	} else if (post && path == QLatin1String("/api/poll-clear")) {
		/* Off the screen; a poll still open is closed first. */
		m_pollTimer->stop();
		m_poll = Poll::Session();
		savePoll();
		broadcastPoll();
		jsonReply(reply, 200, pollMessage());
	} else if (get && path == QLatin1String("/api/subathon")) {
		jsonReply(reply, 200, subathonMessage());
	} else if (post && path == QLatin1String("/api/subathon-config")) {
		m_subathonConfig = Subathon::normalizeConfig(body);
		saveSubathon();
		broadcastSubathon();
		jsonReply(reply, 200, subathonMessage());
	} else if (post && path == QLatin1String("/api/subathon-control")) {
		/* {action: start|pause|resume|add|set|reset, seconds}. */
		const QString action = body.value(QStringLiteral("action")).toString();
		const auto seconds = static_cast<qint64>(body.value(QStringLiteral("seconds")).toDouble());
		const qint64 at = now();
		if (action == QLatin1String("start"))
			m_subathon.start(seconds, at);
		else if (action == QLatin1String("pause"))
			m_subathon.pause(at);
		else if (action == QLatin1String("resume"))
			m_subathon.resume(at);
		else if (action == QLatin1String("add"))
			m_subathon.add(seconds, at);
		else if (action == QLatin1String("set"))
			m_subathon.set(seconds, at);
		else if (action == QLatin1String("reset"))
			m_subathon.reset();
		saveSubathon();
		broadcastSubathon(action == QLatin1String("add") ? seconds : 0);
		jsonReply(reply, 200, subathonMessage());
	} else {
		return false;
	}
	return true;
}
