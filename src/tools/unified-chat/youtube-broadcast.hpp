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

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

/* YouTube stopped making a "stream now" broadcast on its own in 2020: the
 * Studio live page makes one when it opens, and video sent to the stream key
 * without one never goes live. Before a YouTube output starts, the plugin
 * finds or makes that broadcast. These are the decisions, apart from the
 * network calls, so the tests can check them. */
namespace YouTubeBroadcast {

/* liveStreams.list answer -> the id of the stream whose key is `key`. */
QString streamIdForKey(const QJsonObject &streams, const QString &key);

/* liveBroadcasts.list (status "all") answer -> a broadcast bound to
 * streamId that can still take video (created, ready, testing or live). */
QString reusable(const QJsonObject &broadcasts, const QString &streamId);

/* The most recent finished broadcast in the same answer, to copy from. */
QJsonObject lastFinished(const QJsonObject &broadcasts);

/* liveBroadcasts.insert body: title, description and privacy of `last`
 * (or the fallback title, public), starting and ending with the video. */
QJsonObject newBroadcast(const QJsonObject &last, const QString &fallbackTitle, const QDateTime &now);

} // namespace YouTubeBroadcast
