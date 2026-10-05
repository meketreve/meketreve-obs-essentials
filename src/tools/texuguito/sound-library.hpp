/*
Meketreve OBS Essentials - Texuguito
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
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

/* A group of !tocar sounds: its own name, price, wait between two of its
 * sounds and an on/off switch. The price is not what tells groups apart:
 * two groups may cost the same. */
struct SoundGroup {
	QString id;
	QString name;
	int price = 50;
	int cooldown = 30; /* seconds */
	bool enabled = true;
	QStringList sounds; /* in the order the web panel shows them */
};

/* Which sound is in which group, kept in sound-groups.json; the files stay
 * in the audio folder (loose, or one level of subfolders), new sounds go
 * loose in it. No OBS dependency, so it runs in unit tests. */
class SoundLibrary {
public:
	static constexpr int kMaxGroups = 50;
	static constexpr int kMaxCooldown = 3600;

	/* What sync() needs to name and set up the groups it makes. */
	struct Defaults {
		QString looseGroupName;      /* files dropped by hand */
		QStringList otherLooseNames; /* its default name in the other languages */
	};

	explicit SoundLibrary(const QString &jsonPath);

	/* Reads the json and the audio folder: a sound whose file is gone
	 * leaves its group; a file in no group joins the group of new sounds,
	 * which starts turned off so nothing plays before it has a price. Saves
	 * when something changed. */
	void sync(const QString &audioDir, const Defaults &defaults);

	const QList<SoundGroup> &groups() const { return m_groups; }
	const SoundGroup *group(const QString &id) const;
	const SoundGroup *groupOf(const QString &sound) const;
	/* The file of a sound, relative to the audio folder ("buzina.mp3",
	 * "50/buzina.mp3"); empty when there is no such sound. */
	QString file(const QString &sound) const { return m_files.value(sound); }

	/* Creates (empty id) or changes a group; answers its id, or empty when
	 * the name is empty, the id unknown or there are too many groups. */
	QString saveGroup(SoundGroup group);
	/* Only an empty group can go: its sounds would be left without one. */
	bool deleteGroup(const QString &id);
	/* Into that group, at that place (-1 = last); dragging in the panel. */
	bool move(const QString &sound, const QString &groupId, int index = -1);
	/* A group of that price for !addaudio <price>: the first one turned on,
	 * or a new one named name. */
	QString groupForPrice(int price, const QString &name);

	/* After the file was saved, renamed or deleted. */
	void added(const QString &sound, const QString &file, const QString &groupId);
	void renamed(const QString &from, const QString &to, const QString &file);
	void removed(const QString &sound);

private:
	void save() const;
	QString newId() const;
	SoundGroup *find(const QString &id);

	QString m_path;
	QList<SoundGroup> m_groups;
	QHash<QString, QString> m_files; /* sound -> file */
};
