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
#include "items.hpp"
#include "bot-data.hpp"

#include <QJsonArray>
#include <QRandomGenerator>

#include <algorithm>

namespace {

const char *const kRuleIds[] = {"off", "level", "adventure", "boss", "mvp"};

} // namespace

namespace Items {

const QList<Item> &catalog()
{
	/* Recolors of the LPC layers (build-aux/recolor-lpc.py). */
	static const QList<Item> items{
		{QStringLiteral("capareal"), QStringLiteral("royalcape"), Slot::Accessory,
		 QStringLiteral("excl_capareal.png"), Rule::Level, 3},
		{QStringLiteral("coroarubi"), QStringLiteral("rubycrown"), Slot::Hat,
		 QStringLiteral("excl_coroarubi.png"), Rule::Level, 5},
		{QStringLiteral("chifresdourados"), QStringLiteral("goldenhorns"), Slot::Hat,
		 QStringLiteral("excl_chifresdourados.png"), Rule::Level, 10},
		{QStringLiteral("asasdouradas"), QStringLiteral("goldenwings"), Slot::Accessory,
		 QStringLiteral("excl_asasdouradas_bg.png"), Rule::Level, 15},
		{QStringLiteral("magoestelar"), QStringLiteral("starhat"), Slot::Hat,
		 QStringLiteral("excl_magoestelar.png"), Rule::Adventure, 10},
		{QStringLiteral("elmosombrio"), QStringLiteral("darkhelm"), Slot::Hat,
		 QStringLiteral("excl_elmosombrio.png"), Rule::Boss, 0},
		{QStringLiteral("asaschefao"), QStringLiteral("bosswings"), Slot::Accessory,
		 QStringLiteral("excl_asaschefao_bg.png"), Rule::Mvp, 0},
	};
	return items;
}

const Item *find(const QString &id)
{
	for (const Item &item : catalog()) {
		if (item.id == id)
			return &item;
	}
	return nullptr;
}

QString resolve(const QString &raw, Slot slot)
{
	QString value = raw.trimmed().toLower();
	value.remove(QLatin1Char(' '));
	for (const Item &item : catalog()) {
		if (item.slot == slot && (item.id == value || item.english == value))
			return item.id;
	}
	return QString();
}

QString ruleId(Rule rule)
{
	return QLatin1String(kRuleIds[static_cast<int>(rule)]);
}

std::optional<Rule> ruleFromId(const QString &id)
{
	for (int i = 0; i < static_cast<int>(std::size(kRuleIds)); ++i) {
		if (id == QLatin1String(kRuleIds[i]))
			return static_cast<Rule>(i);
	}
	return std::nullopt;
}

} // namespace Items

ItemBook::ItemBook(const QString &path, Chooser choose) : m_path(path), m_choose(std::move(choose))
{
	if (!m_choose)
		m_choose = [](int n) {
			return static_cast<int>(QRandomGenerator::global()->bounded(n));
		};
	load();
}

void ItemBook::load()
{
	const QJsonObject raw = BotData::readJsonFile(m_path);
	m_rules.clear();
	m_owned.clear();
	const QJsonArray rules = raw.value(QStringLiteral("rules")).toArray();
	for (const QJsonValue v : rules) {
		const QJsonObject o = v.toObject();
		const QString id = o.value(QStringLiteral("id")).toString();
		const std::optional<Items::Rule> rule = Items::ruleFromId(o.value(QStringLiteral("rule")).toString());
		if (Items::find(id) && rule)
			m_rules.insert(id, {*rule, std::clamp(o.value(QStringLiteral("value")).toInt(), 0, kMaxLevel)});
	}
	const QJsonObject owned = raw.value(QStringLiteral("owned")).toObject();
	for (auto it = owned.begin(); it != owned.end(); ++it) {
		QStringList ids;
		for (const QJsonValue id : it.value().toArray()) {
			if (Items::find(id.toString()) && !ids.contains(id.toString()))
				ids.append(id.toString());
		}
		if (!ids.isEmpty())
			m_owned.insert(it.key().toLower(), ids);
	}
}

void ItemBook::save() const
{
	QJsonObject owned;
	for (auto it = m_owned.constBegin(); it != m_owned.constEnd(); ++it)
		owned.insert(it.key(), QJsonArray::fromStringList(it.value()));
	QJsonObject root = settingsJson();
	root.insert(QStringLiteral("owned"), owned);
	BotData::writeJsonFile(m_path, root);
}

Items::Rule ItemBook::rule(const QString &id) const
{
	const auto it = m_rules.constFind(id);
	if (it != m_rules.constEnd())
		return it->rule;
	const Items::Item *item = Items::find(id);
	return item ? item->rule : Items::Rule::Off;
}

int ItemBook::value(const QString &id) const
{
	const auto it = m_rules.constFind(id);
	if (it != m_rules.constEnd())
		return it->value;
	const Items::Item *item = Items::find(id);
	return item ? item->value : 0;
}

QJsonObject ItemBook::settingsJson() const
{
	QJsonArray rules;
	for (const Items::Item &item : Items::catalog())
		rules.append(QJsonObject{{QStringLiteral("id"), item.id},
					 {QStringLiteral("rule"), Items::ruleId(rule(item.id))},
					 {QStringLiteral("value"), value(item.id)}});
	return QJsonObject{{QStringLiteral("rules"), rules}};
}

void ItemBook::applySettings(const QJsonObject &settings)
{
	for (const QJsonValue v : settings.value(QStringLiteral("rules")).toArray()) {
		const QJsonObject o = v.toObject();
		const QString id = o.value(QStringLiteral("id")).toString();
		const std::optional<Items::Rule> r = Items::ruleFromId(o.value(QStringLiteral("rule")).toString());
		if (!Items::find(id) || !r)
			continue;
		/* A chance is a percentage; a level, up to kMaxLevel. */
		const int high = *r == Items::Rule::Adventure ? 100 : kMaxLevel;
		const int low = *r == Items::Rule::Level || *r == Items::Rule::Adventure ? 1 : 0;
		m_rules.insert(id, {*r, std::clamp(o.value(QStringLiteral("value")).toInt(value(id)), low, high)});
	}
	save();
}

void ItemBook::restoreDefaults()
{
	m_rules.clear();
	save();
}

bool ItemBook::owns(const QString &key, const QString &id) const
{
	return m_owned.value(key.toLower()).contains(id);
}

QStringList ItemBook::owned(const QString &key) const
{
	QStringList ids;
	const QStringList mine = m_owned.value(key.toLower());
	for (const Items::Item &item : Items::catalog()) {
		if (mine.contains(item.id))
			ids.append(item.id);
	}
	return ids;
}

int ItemBook::owners(const QString &id) const
{
	int count = 0;
	for (const QStringList &ids : m_owned) {
		if (ids.contains(id))
			++count;
	}
	return count;
}

bool ItemBook::unlock(const QString &key, const QString &id)
{
	if (key.isEmpty() || !Items::find(id) || owns(key, id))
		return false;
	m_owned[key.toLower()].append(id);
	save();
	return true;
}

QStringList ItemBook::earn(const QString &key, const std::function<bool(const Items::Item &, const Setting &)> &earned)
{
	QStringList fresh;
	for (const Items::Item &item : Items::catalog()) {
		if (owns(key, item.id))
			continue;
		if (earned(item, {rule(item.id), value(item.id)}) && unlock(key, item.id))
			fresh.append(item.id);
	}
	return fresh;
}

QStringList ItemBook::onLevel(const QString &key, int level)
{
	return earn(key, [level](const Items::Item &, const Setting &s) {
		return s.rule == Items::Rule::Level && level >= s.value;
	});
}

QStringList ItemBook::onAdventure(const QString &key)
{
	return earn(key, [this](const Items::Item &, const Setting &s) {
		return s.rule == Items::Rule::Adventure && m_choose(100) < s.value;
	});
}

QStringList ItemBook::onBoss(const QString &key, bool mvp)
{
	return earn(key, [mvp](const Items::Item &, const Setting &s) {
		return s.rule == Items::Rule::Boss || (mvp && s.rule == Items::Rule::Mvp);
	});
}
