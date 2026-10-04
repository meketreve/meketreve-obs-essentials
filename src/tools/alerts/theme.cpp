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

#include "theme.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Theme {

namespace {

QString color(const QJsonObject &o, const char *key, const QString &fallback)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString v = o.value(QLatin1String(key)).toString();
	return re.match(v).hasMatch() ? v : fallback;
}

} // namespace

const QStringList &targets()
{
	static const QStringList list{QStringLiteral("alertas"), QStringLiteral("chat"),    QStringLiteral("eventos"),
				      QStringLiteral("metas"),   QStringLiteral("enquete"), QStringLiteral("subathon")};
	return list;
}

QJsonObject normalize(const QJsonObject &stored)
{
	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	const QJsonValue opacity = stored.value(QStringLiteral("bubbleOpacity"));
	const double o = opacity.isDouble() && std::isfinite(opacity.toDouble()) ? opacity.toDouble() : 45;
	const QJsonValue shadow = stored.value(QStringLiteral("shadow"));
	const QJsonObject inTargets = stored.value(QStringLiteral("targets")).toObject();
	QJsonObject on;
	for (const QString &t : targets())
		on.insert(t, inTargets.value(t).toBool(true));
	return QJsonObject{
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("accent"), color(stored, "accent", QStringLiteral("#8B5CF6"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), std::clamp(std::round(o), 0.0, 100.0)},
		{QStringLiteral("shadow"), shadow.isBool() ? shadow.toBool() : true},
		{QStringLiteral("targets"), on},
	};
}

QJsonObject applyTo(QJsonObject config, const QJsonObject &theme, const QString &accentKey)
{
	const auto copy = [&config, &theme](const QString &to, const QString &from) {
		if (config.contains(to))
			config.insert(to, theme.value(from));
	};
	for (const char *key : {"font", "textColor", "bubbleColor", "bubbleOpacity", "shadow"})
		copy(QLatin1String(key), QLatin1String(key));
	copy(accentKey, QStringLiteral("accent"));
	return config;
}

QJsonObject applyToAlerts(QJsonObject alerts, const QJsonObject &theme)
{
	QJsonObject types = alerts.value(QStringLiteral("types")).toObject();
	for (auto it = types.begin(); it != types.end(); ++it)
		it.value() = applyTo(it.value().toObject(), theme);
	alerts.insert(QStringLiteral("types"), types);
	return alerts;
}

} // namespace Theme
