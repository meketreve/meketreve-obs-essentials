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

#include <QString>

/* Chat text as HTML for the chat views: escaped, with every emoji sequence
 * wrapped in a span that asks for the color emoji font by name. Qt (6.4 on
 * Linux at least) otherwise falls back to a black-and-white font for some
 * emoji and draws skin tones and flags as boxes or letters. */
QString chatHtml(const QString &text);

/* Exposed for tests. */
bool isEmojiCodePoint(char32_t cp);
