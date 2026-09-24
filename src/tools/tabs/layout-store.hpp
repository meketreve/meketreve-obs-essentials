/*
Meketreve OBS Essentials - Tabs
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

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

/* Where one dock goes in a declarative layout. Areas: left, right, top,
 * bottom. Used by the built-in Live/Build layouts and by presets, which
 * cannot carry a saveState() blob. */
struct DockPlacement {
	QString dock;
	QString area;
};

/* A tab is a named QMainWindow::saveState() snapshot. "live" and "build" are
 * fixed tabs with a built-in default layout; "mine" is the layout the user
 * had before the plugin was installed; everything else is user-made. */
struct TabLayout {
	QString id;
	QString name; /* empty for fixed tabs: the UI shows the translated name */
	QByteArray state;
	QList<DockPlacement> docks; /* used while state is empty */
	bool previewShown = false;

	bool isFixed() const { return id == QLatin1String("live") || id == QLatin1String("build"); }
	bool isRemovable() const { return !isFixed() && id != QLatin1String("mine"); }
};

/* Built-in declarative layouts of the fixed tabs. */
QList<DockPlacement> defaultDocks(const QString &fixedId);

struct TabsConfig {
	static constexpr int kFormat = 1;

	QString current;
	QList<TabLayout> tabs;

	/* First run: "mine" holds the current layout, Live/Build start empty
	 * and get their default layout the first time they are opened. */
	static TabsConfig defaults(const QByteArray &currentState, const QString &myLayoutName);

	qsizetype indexOf(const QString &id) const;
	QString newCustomId() const;

	/* withStates=false leaves the saveState blobs out: those only make
	 * sense on the machine (and OBS build) that produced them. */
	QJsonObject toJson(bool withStates = true) const;
	/* fillFixed adds Live/Build when missing, which a saved profile needs
	 * but an imported partial set of tabs must not get. */
	static bool fromJson(const QJsonObject &obj, TabsConfig &out, QString *error = nullptr, bool fillFixed = true);
};
