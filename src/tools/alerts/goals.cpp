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

#include "goals.hpp"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Goals {

namespace {

constexpr double kMaxAmount = 1e9;

double number(const QJsonObject &o, const char *key, double fallback, double low, double high)
{
	const QJsonValue v = o.value(QLatin1String(key));
	const double n = v.isDouble() ? v.toDouble() : fallback;
	return std::isfinite(n) ? std::clamp(n, low, high) : fallback;
}

bool flag(const QJsonObject &o, const char *key, bool fallback)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() ? v.toBool() : fallback;
}

QString color(const QJsonObject &o, const char *key, const QString &fallback)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString v = o.value(QLatin1String(key)).toString();
	return re.match(v).hasMatch() ? v : fallback;
}

/* Two decimals at most: donations are money, the rest whole numbers. */
double rounded(double value)
{
	return std::round(std::clamp(value, 0.0, kMaxAmount) * 100.0) / 100.0;
}

QJsonArray goalsOf(const QJsonObject &config)
{
	return config.value(QStringLiteral("goals")).toArray();
}

} // namespace

const QStringList &kinds()
{
	static const QStringList list{QStringLiteral("follows"),   QStringLiteral("subs"),    QStringLiteral("bits"),
				      QStringLiteral("donations"), QStringLiteral("members"), QStringLiteral("gifts")};
	return list;
}

QJsonObject normalize(const QJsonObject &stored, const Alerts::TextLookup &text, const QJsonObject &previous)
{
	QHash<QString, double> reached;
	for (const QJsonValue v : goalsOf(previous)) {
		const QJsonObject g = v.toObject();
		reached.insert(g.value(QStringLiteral("id")).toString(), g.value(QStringLiteral("current")).toDouble());
	}

	QJsonArray in = goalsOf(stored);
	if (!stored.contains(QStringLiteral("goals")))
		in.append(QJsonObject{{QStringLiteral("title"), text("Goals.Default.Title")},
				      {QStringLiteral("kind"), QStringLiteral("follows")},
				      {QStringLiteral("target"), 100}});

	static const QRegularExpression validId(QStringLiteral("^[a-z0-9-]{1,24}$"));
	/* A new goal never takes the id of a removed one: it would get its count. */
	QSet<QString> used(reached.keyBegin(), reached.keyEnd());
	for (const QJsonValue v : in) {
		const QString id = v.toObject().value(QStringLiteral("id")).toString();
		if (validId.match(id).hasMatch())
			used.insert(id);
	}
	QSet<QString> taken;
	int serial = 1;
	QJsonArray goals;
	for (const QJsonValue v : in) {
		if (goals.size() >= kMaxGoals)
			break;
		const QJsonObject g = v.toObject();
		QString id = g.value(QStringLiteral("id")).toString();
		if (!validId.match(id).hasMatch() || taken.contains(id)) {
			do
				id = QStringLiteral("meta-%1").arg(serial++);
			while (used.contains(id) || taken.contains(id));
		}
		taken.insert(id);
		const QString kind = g.value(QStringLiteral("kind")).toString();
		const double current = reached.contains(id) ? reached.value(id)
							    : number(g, "current", 0, 0, kMaxAmount);
		goals.append(QJsonObject{
			{QStringLiteral("id"), id},
			{QStringLiteral("title"), g.value(QStringLiteral("title")).toString().trimmed().left(80)},
			{QStringLiteral("kind"), kinds().contains(kind) ? kind : QStringLiteral("follows")},
			{QStringLiteral("target"), rounded(std::max(1.0, number(g, "target", 100, 1, kMaxAmount)))},
			{QStringLiteral("current"), rounded(current)},
			{QStringLiteral("prefix"), g.value(QStringLiteral("prefix")).toString().left(8)},
			{QStringLiteral("resetOnLive"), flag(g, "resetOnLive", false)},
		});
	}

	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	return QJsonObject{
		{QStringLiteral("goals"), goals},
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("fontSize"), number(stored, "fontSize", 24, 12, 72)},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("accent"), color(stored, "accent", QStringLiteral("#8B5CF6"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), number(stored, "bubbleOpacity", 45, 0, 100)},
		{QStringLiteral("shadow"), flag(stored, "shadow", true)},
	};
}

double amountFor(const QString &kind, const Alerts::Event &event)
{
	const QString &type = event.type;
	const double count = std::max(1.0, event.value);
	if (kind == QLatin1String("follows"))
		return type == QLatin1String("follow") ? 1 : 0;
	if (kind == QLatin1String("subs")) {
		/* A resub's value is the months, not more subs. */
		if (type == QLatin1String("sub") || type == QLatin1String("resub"))
			return 1;
		return type == QLatin1String("giftsub") ? count : 0;
	}
	if (kind == QLatin1String("gifts"))
		return type == QLatin1String("giftsub") || type == QLatin1String("gift") ? count : 0;
	if (kind == QLatin1String("members"))
		return type == QLatin1String("membership") ? 1 : 0;
	if (kind == QLatin1String("bits"))
		return type == QLatin1String("bits") ? std::max(0.0, event.value) : 0;
	if (kind == QLatin1String("donations"))
		return type == QLatin1String("donation") ? std::max(0.0, event.value) : 0;
	return 0;
}

bool apply(QJsonObject &config, const Alerts::Event &event)
{
	if (event.test)
		return false;
	QJsonArray goals = goalsOf(config);
	bool changed = false;
	for (qsizetype i = 0; i < goals.size(); i++) {
		QJsonObject g = goals.at(i).toObject();
		const double add = amountFor(g.value(QStringLiteral("kind")).toString(), event);
		if (add <= 0)
			continue;
		g.insert(QStringLiteral("current"), rounded(g.value(QStringLiteral("current")).toDouble() + add));
		goals.replace(i, g);
		changed = true;
	}
	if (changed)
		config.insert(QStringLiteral("goals"), goals);
	return changed;
}

bool adjust(QJsonObject &config, const QString &id, double value, bool set)
{
	if (!std::isfinite(value))
		return false;
	QJsonArray goals = goalsOf(config);
	for (qsizetype i = 0; i < goals.size(); i++) {
		QJsonObject g = goals.at(i).toObject();
		if (g.value(QStringLiteral("id")).toString() != id)
			continue;
		const double now = set ? value : g.value(QStringLiteral("current")).toDouble() + value;
		g.insert(QStringLiteral("current"), rounded(now));
		goals.replace(i, g);
		config.insert(QStringLiteral("goals"), goals);
		return true;
	}
	return false;
}

bool resetForLive(QJsonObject &config)
{
	QJsonArray goals = goalsOf(config);
	bool changed = false;
	for (qsizetype i = 0; i < goals.size(); i++) {
		QJsonObject g = goals.at(i).toObject();
		if (!g.value(QStringLiteral("resetOnLive")).toBool() ||
		    g.value(QStringLiteral("current")).toDouble() <= 0)
			continue;
		g.insert(QStringLiteral("current"), 0);
		goals.replace(i, g);
		changed = true;
	}
	if (changed)
		config.insert(QStringLiteral("goals"), goals);
	return changed;
}

} // namespace Goals
