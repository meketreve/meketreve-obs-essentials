/*
Meketreve OBS Essentials - Shared configuration
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

/* The shareable configuration is a JSON object
 *   { "format": 1, "name": "...", "tabs": {...}, "chat": {...}, "outputs": [...] }
 * carried as a single line of text: "MOE1:" + base64url(qCompress(json)).
 * Stream keys and login tokens are never part of it. */
namespace ConfigCodec {

constexpr int kFormat = 1;
constexpr const char *kPrefix = "MOE1:";
/* Guards against a pasted string that inflates to something huge. */
constexpr qsizetype kMaxJsonBytes = 4 * 1024 * 1024;

QString encode(const QJsonObject &bundle);
bool decode(const QString &text, QJsonObject &bundle, QString *error = nullptr);
/* Checks "format" on a bundle that came from a file (the presets). */
bool validate(const QJsonObject &bundle, QString *error = nullptr);

} // namespace ConfigCodec
