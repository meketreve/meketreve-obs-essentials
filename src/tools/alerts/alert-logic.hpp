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
#pragma once

#include "../unified-chat/chat-connector.hpp"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

/* Everything about alerts that does not need OBS: which chat events become
 * which alert, the config with its defaults, the minimum-value filter and the
 * checks that keep other web sites away from the editor's API. */
namespace Alerts {

using TextLookup = std::function<QString(const char *key)>;

struct Event {
	QString type;     /* follow, sub, resub, giftsub, bits, donation, raid, ... */
	QString name;     /* who did it */
	double value = 0; /* what the minimum is compared with */
	QString amount;   /* {quantidade} as shown: "5", "R$ 10,00" */
	QString detail;   /* gift name, tier */
	QString message;  /* what they typed */
	QString platform; /* twitch, youtube, kick, trovo */
	bool test = false;
};

/* In the order the editor lists them. */
const QStringList &types();
bool isType(const QString &type);

/* Empty type when the message is not an event. */
Event fromChat(const ChatMessage &msg);
/* "R$ 10,00", "$5.00", "€1.234,56", "¥500" -> 10, 5, 1234.56, 500. */
double parseMoney(const QString &text);
QString platformKey(ChatPlatform platform);

/* A made-up event of that type for the "Test" buttons. */
Event sample(const QString &type, const TextLookup &text);

QJsonObject defaults(const TextLookup &text);
/* Fills what is missing from the defaults, clamps numbers and drops
 * anything unknown, so the pages can trust what they get. */
QJsonObject normalize(const QJsonObject &config, const TextLookup &text);
/* The config the overlay sees: no API keys. */
QJsonObject forOverlay(const QJsonObject &config);
bool passes(const QJsonObject &config, const Event &event);
QJsonObject toJson(const Event &event);

/* "", "builtin:<id>", "media:<file>" or an http(s) link; anything else -> "". */
QString cleanReference(const QString &value);
/* A safe file name for an upload, or empty when the type is not allowed. */
QString safeMediaName(const QString &name);
QString mediaKind(const QString &name); /* image, video, audio or "" */

/* The editor's API only answers pages served by this server. */
bool isLocalHost(const QByteArray &host, quint16 port);
bool isLocalOrigin(const QByteArray &origin, quint16 port);

/* Twitch sends "X gifted 5 subs" and then one "X gifted a sub to Y" per
 * sub; only the first becomes an alert. */
class GiftDedup {
public:
	/* True when the event is one of those follow-ups. */
	bool swallow(const Event &event, qint64 nowMs);

private:
	struct Pending {
		int left = 0;
		qint64 until = 0;
	};
	QHash<QString, Pending> m_pending;
};

} // namespace Alerts
