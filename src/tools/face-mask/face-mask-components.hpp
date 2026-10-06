/*
Meketreve OBS Essentials - Face mask
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
#include <QtGlobal>

#include <vector>

namespace FaceMask {

/* A file the face mask downloads on first use, as published in the
 * components release (meketreve/meketreve-obs-essentials-components). The
 * size and SHA-256 are fixed here: anything else is refused. */
struct Component {
	const char *file;
	bool model; /* in face-mask/models/, else in face-mask/ */
	qint64 size;
	const char *sha256;
};

/* The release the files come from. */
constexpr const char *kComponentsRelease =
	"https://github.com/meketreve/meketreve-obs-essentials-components/releases/download/face-mask-1/";

/* onnxruntime for this system and the three models. */
const std::vector<Component> &components();
QString componentUrl(const Component &component);
qint64 componentsBytes();

/* The file at path has exactly this size and SHA-256 (lowercase hex). */
bool fileMatches(const QString &path, qint64 size, const QString &sha256);

} // namespace FaceMask
