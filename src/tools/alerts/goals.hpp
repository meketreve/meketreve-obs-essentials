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

#include "alert-logic.hpp"

#include <QJsonObject>
#include <QStringList>

/* The goals overlay at /metas: progress bars that the events of the three
 * platforms fill ("100 follows", "R$ 500 in donations"). With "?meta=<id>"
 * it shows one goal. The config is plain JSON, as the editor sends it:
 * {goals: [{id, title, kind, target, current, prefix, resetOnLive}], font,
 * fontSize, textColor, accent, bubbleColor, bubbleOpacity, shadow}. Pure
 * logic, so the tests run without OBS. */
namespace Goals {

constexpr int kMaxGoals = 20;

/* follows, subs, bits, donations, members, gifts. */
const QStringList &kinds();

/* Fills what is missing, clamps what is out of range and gives every goal
 * an id; without a "goals" list there is one follows goal to start from.
 * The amounts reached ("current") come from previous when the goal already
 * existed there: the editor can save while events arrive. */
QJsonObject normalize(const QJsonObject &stored, const Alerts::TextLookup &text,
		      const QJsonObject &previous = QJsonObject());

/* What the event adds to a goal of that kind (0 when nothing): a sub is one
 * sub, five gifted subs are five subs and five gifts, bits and donations add
 * their value. */
double amountFor(const QString &kind, const Alerts::Event &event);

/* Adds the event to every goal it counts for; true when one changed. */
bool apply(QJsonObject &config, const Alerts::Event &event);
/* The "+ / -" and "Reset" buttons: adds delta to the goal, or sets it when
 * set is true. Never below zero. False when there is no such goal. */
bool adjust(QJsonObject &config, const QString &id, double value, bool set);
/* A new live: the goals marked "resetOnLive" start from zero. */
bool resetForLive(QJsonObject &config);

} // namespace Goals
