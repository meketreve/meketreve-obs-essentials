/*
Meketreve OBS Essentials - Now Playing
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
#include <QString>

/* The song the computer is playing, from the system media controls. */
struct MediaInfo {
	QString player; /* "spotify", "firefox", "vlc"... */
	bool playing = false;
	QString title, artist, album;
	QString art;                     /* http(s) or file:// */
	double length = 0, position = 0; /* seconds; 0 = unknown */

	bool isValid() const { return !player.isEmpty(); }
	QJsonObject toJson() const;
};

/* Asks the media players (MPRIS on Linux; nothing elsewhere yet), preferring
 * one that is playing over a paused one. Blocks for a few hundred ms at most:
 * call it off the UI thread. */
MediaInfo currentMedia();
