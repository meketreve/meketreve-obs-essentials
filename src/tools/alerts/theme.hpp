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

#include <QJsonObject>
#include <QStringList>

/* The web panel's Theme tab: one font and one set of colors written into
 * every overlay's config at once. It is a copy, not a link: each overlay can
 * still be changed on its own afterwards. Pure logic, so the tests run
 * without OBS. */
namespace Theme {

/* alertas, chat, eventos, metas, enquete, subathon. */
const QStringList &targets();

/* {font, textColor, accent, bubbleColor, bubbleOpacity, shadow, targets:
 * {<target>: bool}}. */
QJsonObject normalize(const QJsonObject &stored);

/* Writes the theme's look into an overlay config, only the keys the config
 * already has; accentKey is where that config keeps its accent color. */
QJsonObject applyTo(QJsonObject config, const QJsonObject &theme, const QString &accentKey = QStringLiteral("accent"));
/* The alerts config: the font, text color and accent of every alert type. */
QJsonObject applyToAlerts(QJsonObject alerts, const QJsonObject &theme);

} // namespace Theme
