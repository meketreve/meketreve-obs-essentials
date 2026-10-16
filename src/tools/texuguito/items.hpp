/*
Meketreve OBS Essentials - Texuguito bot
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

/* Exclusive items for the parade: hats, capes and wings that only show up
 * on whoever earned them, by reaching a level, coming back from an
 * adventure with luck, or helping to beat a boss (or being its MVP). The
 * streamer picks how each one is earned; an item once earned stays. Worn
 * with !hat/!accessory like any other. Saved in items.json. No OBS
 * dependency, so it runs in unit tests. */

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Items {

enum class Slot { Hat, Accessory };
enum class Rule { Off, Level, Adventure, Boss, Mvp };

struct Item {
	QString id;      /* also the sprite key in manifest.json */
	QString english; /* the English name for the commands */
	Slot slot = Slot::Hat;
	QString preview; /* a sprite sheet to show it in the panel */
	Rule rule = Rule::Off;
	int value = 0; /* the level, or the chance per adventure in % */
};

/* Every exclusive item, with the default way to earn it. */
const QList<Item> &catalog();
const Item *find(const QString &id);
/* An id or English name of an item of that slot, as typed; empty if none. */
QString resolve(const QString &raw, Slot slot);
QString ruleId(Rule rule);
std::optional<Rule> ruleFromId(const QString &id);

} // namespace Items

class ItemBook {
public:
	static constexpr int kMaxLevel = 1000;

	/* Random index in [0, n), for the adventure chance; tests pass their own. */
	using Chooser = std::function<int(int n)>;
	explicit ItemBook(const QString &path, Chooser choose = Chooser());
	void setChooser(Chooser choose) { m_choose = std::move(choose); }

	void load();
	void save() const;

	/* How id is earned now: the catalog's default or the streamer's rule. */
	Items::Rule rule(const QString &id) const;
	int value(const QString &id) const;
	/* {rules: [{id, rule, value}]}, for the web panel; applySettings takes
	 * the same (what is left out keeps its rule). */
	QJsonObject settingsJson() const;
	void applySettings(const QJsonObject &settings);
	void restoreDefaults();

	bool owns(const QString &key, const QString &id) const;
	/* In catalog order. */
	QStringList owned(const QString &key) const;
	int owners(const QString &id) const;
	/* Gives id to key; true when it is new. */
	bool unlock(const QString &key, const QString &id);

	/* What key earns now; each answers only the new ones. */
	QStringList onLevel(const QString &key, int level);
	QStringList onAdventure(const QString &key);
	QStringList onBoss(const QString &key, bool mvp);

private:
	struct Setting {
		Items::Rule rule = Items::Rule::Off;
		int value = 0;
	};
	QStringList earn(const QString &key, const std::function<bool(const Items::Item &, const Setting &)> &earned);

	QString m_path;
	Chooser m_choose;
	QHash<QString, Setting> m_rules;     /* by item id; missing = the default */
	QHash<QString, QStringList> m_owned; /* by viewer (the bot passes the name, without platform) */
};
