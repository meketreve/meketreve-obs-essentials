/*
Meketreve OBS Essentials - Updater
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

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QUrl>

/* The parts of the updater that need neither OBS nor a network, so the
 * tests can reach them. */
namespace UpdateLogic {

constexpr const char *kLatestReleaseApi =
	"https://api.github.com/repos/meketreve/meketreve-obs-essentials/releases/latest";

struct Release {
	QString version; /* "1.2.0", from the tag */
	QString notes;   /* release body without the checksum block */
	QUrl page;
	QHash<QString, QUrl> assets;       /* file name -> download URL */
	QHash<QString, QString> checksums; /* file name -> sha256 (hex) */
};

/* Which package the running plugin updates itself with. */
enum class Package { WindowsInstaller, Deb, MacPkg };

Release parseRelease(const QJsonObject &json);
/* "1.10.0" > "1.9.2"; a leading "v" is ignored, a missing part counts as 0. */
int compareVersions(const QString &a, const QString &b);
bool isNewer(const QString &candidate, const QString &current);
/* The "### Checksums" block the release workflow writes: "    <file>: <hex>". */
QHash<QString, QString> parseChecksums(const QString &body);
QString stripChecksums(const QString &body);
/* Notes written in both languages ("<!-- lang:pt -->" … "<!-- lang:en -->"
 * blocks, from next_release.py) give the block of <lang> ("pt" or "en");
 * notes without the markers come back whole. */
QString notesFor(const QString &notes, const QString &lang);
/* The asset for that package ("…-windows-x64-installer.exe", "….deb" but not
 * the "-dbgsym.ddeb", "….pkg"); empty when the release has none. */
QString pickAsset(const Release &release, Package package);

} // namespace UpdateLogic
