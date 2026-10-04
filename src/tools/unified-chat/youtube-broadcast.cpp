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

#include "youtube-broadcast.hpp"

#include <QJsonArray>
#include <QStringList>

namespace YouTubeBroadcast {

namespace {

QJsonObject part(const QJsonObject &item, const char *name)
{
	return item.value(QLatin1String(name)).toObject();
}

QString lifeCycle(const QJsonObject &item)
{
	return part(item, "status").value(QStringLiteral("lifeCycleStatus")).toString();
}

} // namespace

QString streamIdForKey(const QJsonObject &streams, const QString &key)
{
	if (key.trimmed().isEmpty())
		return QString();
	for (const QJsonValue v : streams.value(QStringLiteral("items")).toArray()) {
		const QJsonObject item = v.toObject();
		const QString name =
			part(part(item, "cdn"), "ingestionInfo").value(QStringLiteral("streamName")).toString();
		if (name == key.trimmed())
			return item.value(QStringLiteral("id")).toString();
	}
	return QString();
}

QString reusable(const QJsonObject &broadcasts, const QString &streamId)
{
	static const QStringList open{QStringLiteral("created"),      QStringLiteral("ready"),
				      QStringLiteral("testing"),      QStringLiteral("testStarting"),
				      QStringLiteral("liveStarting"), QStringLiteral("live")};
	if (streamId.isEmpty())
		return QString();
	for (const QJsonValue v : broadcasts.value(QStringLiteral("items")).toArray()) {
		const QJsonObject item = v.toObject();
		if (part(item, "contentDetails").value(QStringLiteral("boundStreamId")).toString() == streamId &&
		    open.contains(lifeCycle(item)))
			return item.value(QStringLiteral("id")).toString();
	}
	return QString();
}

QJsonObject lastFinished(const QJsonObject &broadcasts)
{
	QJsonObject best;
	QDateTime bestEnd;
	for (const QJsonValue v : broadcasts.value(QStringLiteral("items")).toArray()) {
		const QJsonObject item = v.toObject();
		if (lifeCycle(item) != QLatin1String("complete"))
			continue;
		const QDateTime end = QDateTime::fromString(
			part(item, "snippet").value(QStringLiteral("actualEndTime")).toString(), Qt::ISODate);
		if (best.isEmpty() || (end.isValid() && (!bestEnd.isValid() || end > bestEnd))) {
			best = item;
			bestEnd = end;
		}
	}
	return best;
}

QJsonObject newBroadcast(const QJsonObject &last, const QString &fallbackTitle, const QDateTime &now)
{
	const QJsonObject snippet = part(last, "snippet");
	const QJsonObject status = part(last, "status");
	QString title = snippet.value(QStringLiteral("title")).toString().trimmed();
	if (title.isEmpty())
		title = fallbackTitle.trimmed();
	QString privacy = status.value(QStringLiteral("privacyStatus")).toString();
	if (privacy != QLatin1String("unlisted") && privacy != QLatin1String("private"))
		privacy = QStringLiteral("public");
	const QJsonValue kids = status.value(QStringLiteral("selfDeclaredMadeForKids"));
	return QJsonObject{
		{QStringLiteral("snippet"),
		 QJsonObject{{QStringLiteral("title"), title.left(100)},
			     {QStringLiteral("description"), snippet.value(QStringLiteral("description")).toString()},
			     {QStringLiteral("scheduledStartTime"), now.toUTC().toString(Qt::ISODateWithMs)}}},
		{QStringLiteral("status"),
		 QJsonObject{{QStringLiteral("privacyStatus"), privacy},
			     {QStringLiteral("selfDeclaredMadeForKids"), kids.isBool() && kids.toBool()}}},
		/* What the old "stream now" broadcast did by default. */
		{QStringLiteral("contentDetails"),
		 QJsonObject{{QStringLiteral("enableAutoStart"), true}, {QStringLiteral("enableAutoStop"), true}}},
	};
}

} // namespace YouTubeBroadcast
