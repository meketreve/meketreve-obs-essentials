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

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

/* Chat on screen: the Unified Chat as a Browser Source at /chat, styled in
 * the editor at /chat-editor. Pure logic, so the tests run without OBS. */
namespace ChatOverlay {

/* "twitch", "youtube", "kick", in the order the editor lists them. */
QStringList platforms();
QString platformKey(ChatPlatform platform);

QJsonObject defaults();
/* Fills what is missing and clamps what is out of range. */
QJsonObject normalize(const QJsonObject &stored);

/* Commands and hidden users never reach the overlay. The platforms are
 * filtered by the page, so each Browser Source can pick its own. */
bool passes(const QJsonObject &config, const ChatMessage &msg);

/* What the page gets: emote positions are UTF-16 like JavaScript strings. */
QJsonObject toJson(const ChatMessage &msg);

/* Made-up lines for the editor's preview and the "test on stream" button. */
QJsonArray samples();

} // namespace ChatOverlay
