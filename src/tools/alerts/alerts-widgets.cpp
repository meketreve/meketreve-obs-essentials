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

/* The overlays the events fill (goals), apart from alerts-dock.cpp: they
 * share its server, token and events. */

#include "alerts-dock.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

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

} // namespace

void AlertsDock::loadWidgets()
{
	m_goals = Goals::normalize(readJson(QDir(m_dir).filePath(QStringLiteral("goals.json"))), T);
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
}

bool AlertsDock::widgetsPage(const QString &path, OverlayServer::Reply &reply) const
{
	if (path == QLatin1String("/metas"))
		reply.file = webFile("goals.html");
	else if (path == QLatin1String("/metas-editor"))
		reply.file = webFile("goals-editor.html");
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
	if (post && !doc.isObject() && path.startsWith(QLatin1String("/api/goals"))) {
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
	} else {
		return false;
	}
	return true;
}
