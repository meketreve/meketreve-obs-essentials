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

#include "sound-library.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <climits>

namespace {

constexpr int kMaxPrice = 100000;

bool isAudio(const QFileInfo &file)
{
	const QString ext = file.suffix().toLower();
	return ext == QLatin1String("mp3") || ext == QLatin1String("wav") || ext == QLatin1String("ogg");
}

} // namespace

SoundLibrary::SoundLibrary(const QString &jsonPath) : m_path(jsonPath) {}

const SoundGroup *SoundLibrary::group(const QString &id) const
{
	for (const SoundGroup &g : m_groups)
		if (g.id == id)
			return &g;
	return nullptr;
}

SoundGroup *SoundLibrary::find(const QString &id)
{
	for (SoundGroup &g : m_groups)
		if (g.id == id)
			return &g;
	return nullptr;
}

const SoundGroup *SoundLibrary::groupOf(const QString &sound) const
{
	for (const SoundGroup &g : m_groups)
		if (g.sounds.contains(sound))
			return &g;
	return nullptr;
}

QString SoundLibrary::newId() const
{
	for (int n = 1;; n++) {
		const QString id = QStringLiteral("g%1").arg(n);
		if (!group(id))
			return id;
	}
}

void SoundLibrary::sync(const QString &audioDir, const Defaults &defaults)
{
	/* The files: loose in the folder, then in the old price folders. A name
	 * found twice keeps the first file. */
	m_files.clear();
	QHash<QString, int> legacyPrice;
	const QDir root(audioDir);
	for (const QFileInfo &f : root.entryInfoList(QDir::Files, QDir::Name))
		if (isAudio(f))
			m_files.insert(f.completeBaseName().toLower(), f.fileName());
	for (const QString &folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		bool numeric = false;
		const int price = folder.toInt(&numeric);
		if (!numeric || price < 0)
			continue;
		for (const QFileInfo &f : QDir(root.filePath(folder)).entryInfoList(QDir::Files, QDir::Name)) {
			const QString name = f.completeBaseName().toLower();
			if (!isAudio(f) || m_files.contains(name))
				continue;
			m_files.insert(name, folder + QLatin1Char('/') + f.fileName());
			legacyPrice.insert(name, price);
		}
	}

	m_groups.clear();
	QFile in(m_path);
	if (in.open(QIODevice::ReadOnly)) {
		const QJsonArray groups =
			QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("groups")).toArray();
		for (const QJsonValue v : groups) {
			const QJsonObject o = v.toObject();
			SoundGroup g;
			g.id = o.value(QStringLiteral("id")).toString();
			g.name = o.value(QStringLiteral("name")).toString().trimmed().left(40);
			g.price = std::clamp(o.value(QStringLiteral("price")).toInt(50), 0, kMaxPrice);
			g.cooldown = std::clamp(o.value(QStringLiteral("cooldown")).toInt(30), 0, kMaxCooldown);
			g.enabled = o.value(QStringLiteral("enabled")).toBool(true);
			for (const QJsonValue s : o.value(QStringLiteral("sounds")).toArray())
				g.sounds.append(s.toString());
			if (g.id.isEmpty() || group(g.id) || m_groups.size() >= kMaxGroups)
				continue;
			if (g.name.isEmpty())
				g.name = g.id;
			m_groups.append(g);
		}
	}

	/* A sound whose file is gone (or listed twice) leaves its group. */
	bool changed = false;
	QSet<QString> placed;
	for (SoundGroup &g : m_groups) {
		const qsizetype before = g.sounds.size();
		g.sounds.erase(std::remove_if(g.sounds.begin(), g.sounds.end(),
					      [this, &placed](const QString &s) {
						      if (!m_files.contains(s) || placed.contains(s))
							      return true;
						      placed.insert(s);
						      return false;
					      }),
			       g.sounds.end());
		changed = changed || g.sounds.size() != before;
	}

	/* Files in no group yet: the old price folders become groups; a file
	 * dropped in by hand waits, turned off, in the group of new sounds. */
	QHash<int, QString> madeForPrice;
	QString loose;
	/* Cheapest old folder first, so the groups come in price order. */
	QStringList names = m_files.keys();
	std::sort(names.begin(), names.end(), [&legacyPrice](const QString &a, const QString &b) {
		/* Loose files last: they never pick a price group. */
		const int pa = legacyPrice.value(a, INT_MAX), pb = legacyPrice.value(b, INT_MAX);
		return pa != pb ? pa < pb : a < b;
	});
	for (const QString &name : names) {
		if (placed.contains(name))
			continue;
		QString id;
		if (legacyPrice.contains(name)) {
			const int price = legacyPrice.value(name);
			id = madeForPrice.value(price);
			for (const SoundGroup &g : m_groups)
				if (id.isEmpty() && g.price == price && g.id != loose &&
				    (defaults.looseGroupName.isEmpty() || g.name != defaults.looseGroupName))
					id = g.id;
			if (id.isEmpty()) {
				SoundGroup g;
				g.id = newId();
				g.name = defaults.priceGroupName ? defaults.priceGroupName(price)
								 : QString::number(price);
				g.price = price;
				g.cooldown = defaults.priceCooldown ? defaults.priceCooldown(price) : 30;
				m_groups.append(g);
				id = madeForPrice[price] = g.id;
			}
		} else {
			for (const SoundGroup &g : m_groups)
				if (loose.isEmpty() && !defaults.looseGroupName.isEmpty() &&
				    g.name == defaults.looseGroupName)
					loose = g.id;
			if (loose.isEmpty()) {
				SoundGroup g;
				g.id = newId();
				g.name = defaults.looseGroupName.isEmpty() ? g.id : defaults.looseGroupName;
				g.enabled = false;
				m_groups.append(g);
				loose = g.id;
			}
			id = loose;
		}
		find(id)->sounds.append(name);
		changed = true;
	}
	if (changed)
		save();
}

QString SoundLibrary::saveGroup(SoundGroup g)
{
	g.name = g.name.trimmed().left(40);
	if (g.name.isEmpty())
		return QString();
	g.price = std::clamp(g.price, 0, kMaxPrice);
	g.cooldown = std::clamp(g.cooldown, 0, kMaxCooldown);
	if (g.id.isEmpty()) {
		if (m_groups.size() >= kMaxGroups)
			return QString();
		g.id = newId();
		g.sounds.clear();
		m_groups.append(g);
	} else {
		SoundGroup *existing = find(g.id);
		if (!existing)
			return QString();
		g.sounds = existing->sounds;
		*existing = g;
	}
	save();
	return g.id;
}

bool SoundLibrary::deleteGroup(const QString &id)
{
	for (qsizetype i = 0; i < m_groups.size(); i++) {
		if (m_groups.at(i).id != id)
			continue;
		if (!m_groups.at(i).sounds.isEmpty())
			return false;
		m_groups.removeAt(i);
		save();
		return true;
	}
	return false;
}

bool SoundLibrary::move(const QString &sound, const QString &groupId, int index)
{
	SoundGroup *target = find(groupId);
	if (!target || !m_files.contains(sound))
		return false;
	for (SoundGroup &g : m_groups)
		g.sounds.removeAll(sound);
	const qsizetype at = index < 0 ? target->sounds.size() : std::min<qsizetype>(index, target->sounds.size());
	target->sounds.insert(at, sound);
	save();
	return true;
}

QString SoundLibrary::groupForPrice(int price, const QString &name)
{
	for (const SoundGroup &g : m_groups)
		if (g.enabled && g.price == price)
			return g.id;
	SoundGroup g;
	g.name = name;
	g.price = price;
	return saveGroup(g);
}

void SoundLibrary::added(const QString &sound, const QString &file, const QString &groupId)
{
	m_files.insert(sound, file);
	move(sound, groupId);
}

void SoundLibrary::renamed(const QString &from, const QString &to, const QString &file)
{
	m_files.remove(from);
	m_files.insert(to, file);
	for (SoundGroup &g : m_groups)
		std::replace(g.sounds.begin(), g.sounds.end(), from, to);
	save();
}

void SoundLibrary::removed(const QString &sound)
{
	m_files.remove(sound);
	for (SoundGroup &g : m_groups)
		g.sounds.removeAll(sound);
	save();
}

void SoundLibrary::save() const
{
	QJsonArray groups;
	for (const SoundGroup &g : m_groups)
		groups.append(QJsonObject{{QStringLiteral("id"), g.id},
					  {QStringLiteral("name"), g.name},
					  {QStringLiteral("price"), g.price},
					  {QStringLiteral("cooldown"), g.cooldown},
					  {QStringLiteral("enabled"), g.enabled},
					  {QStringLiteral("sounds"), QJsonArray::fromStringList(g.sounds)}});
	QDir().mkpath(QFileInfo(m_path).path());
	QSaveFile out(m_path);
	if (out.open(QIODevice::WriteOnly)) {
		out.write(
			QJsonDocument(QJsonObject{{QStringLiteral("groups"), groups}}).toJson(QJsonDocument::Indented));
		out.commit();
	}
}
