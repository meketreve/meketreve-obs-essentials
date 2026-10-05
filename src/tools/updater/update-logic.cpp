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
#include "update-logic.hpp"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace UpdateLogic {

namespace {

const QString kChecksumHeader = QStringLiteral("### Checksums");

QList<int> versionParts(QString v)
{
	v = v.trimmed();
	if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
		v.remove(0, 1);
	/* "1.2.0-beta1" compares as 1.2.0. */
	v = v.section(QLatin1Char('-'), 0, 0);
	QList<int> parts;
	for (const QString &p : v.split(QLatin1Char('.')))
		parts.append(p.toInt());
	while (parts.size() < 3)
		parts.append(0);
	return parts;
}

} // namespace

Release parseRelease(const QJsonObject &json)
{
	Release r;
	r.version = json.value(QStringLiteral("tag_name")).toString();
	if (r.version.startsWith(QLatin1Char('v')))
		r.version.remove(0, 1);
	const QString body = json.value(QStringLiteral("body")).toString();
	r.notes = stripChecksums(body);
	r.checksums = parseChecksums(body);
	r.page = QUrl(json.value(QStringLiteral("html_url")).toString());
	for (const QJsonValue v : json.value(QStringLiteral("assets")).toArray()) {
		const QJsonObject a = v.toObject();
		r.assets.insert(a.value(QStringLiteral("name")).toString(),
				QUrl(a.value(QStringLiteral("browser_download_url")).toString()));
	}
	return r;
}

int compareVersions(const QString &a, const QString &b)
{
	const QList<int> pa = versionParts(a);
	const QList<int> pb = versionParts(b);
	for (qsizetype i = 0; i < std::max(pa.size(), pb.size()); i++) {
		const int x = i < pa.size() ? pa[i] : 0;
		const int y = i < pb.size() ? pb[i] : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

bool isNewer(const QString &candidate, const QString &current)
{
	return !candidate.isEmpty() && compareVersions(candidate, current) > 0;
}

QHash<QString, QString> parseChecksums(const QString &body)
{
	QHash<QString, QString> sums;
	const qsizetype start = body.indexOf(kChecksumHeader);
	if (start < 0)
		return sums;
	static const QRegularExpression line(QStringLiteral("^\\s*([^\\s:]+):\\s*([0-9a-fA-F]{64})\\s*$"),
					     QRegularExpression::MultilineOption);
	auto it = line.globalMatch(body.mid(start + kChecksumHeader.size()));
	while (it.hasNext()) {
		const auto m = it.next();
		sums.insert(m.captured(1), m.captured(2).toLower());
	}
	return sums;
}

QString notesFor(const QString &notes, const QString &lang)
{
	static const QRegularExpression marker(QStringLiteral("<!--\\s*lang:(\\w+)\\s*-->"));
	QString found;
	QString fallback;
	auto it = marker.globalMatch(notes);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		const qsizetype end = notes.indexOf(QStringLiteral("<!--"), m.capturedEnd());
		const QString block = notes.mid(m.capturedEnd(), end < 0 ? -1 : end - m.capturedEnd()).trimmed();
		if (m.captured(1) == lang)
			found = block;
		else if (m.captured(1) == QLatin1String("en"))
			fallback = block;
	}
	if (!found.isEmpty())
		return found;
	return fallback.isEmpty() ? notes : fallback;
}

QString stripChecksums(const QString &body)
{
	const qsizetype start = body.indexOf(kChecksumHeader);
	return (start < 0 ? body : body.left(start)).trimmed();
}

QString pickAsset(const Release &release, Package package)
{
	for (auto it = release.assets.constBegin(); it != release.assets.constEnd(); ++it) {
		const QString &name = it.key();
		switch (package) {
		case Package::WindowsInstaller:
			if (name.endsWith(QLatin1String("-windows-x64-installer.exe")))
				return name;
			break;
		case Package::Deb:
			if (name.endsWith(QLatin1String(".deb")))
				return name;
			break;
		case Package::MacPkg:
			if (name.endsWith(QLatin1String(".pkg")))
				return name;
			break;
		}
	}
	return QString();
}

} // namespace UpdateLogic
