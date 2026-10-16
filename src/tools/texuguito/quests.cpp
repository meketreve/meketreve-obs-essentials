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
#include "quests.hpp"
#include "bot-data.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace {

const char *const kKindIds[] = {"messages", "minutes",   "dance", "look",    "sound",  "tts", "spend",
				"raffle",   "adventure", "boss",  "support", "follow", "sub"};

const char *announceId(QuestBook::Announce a)
{
	switch (a) {
	case QuestBook::Announce::None:
		return "none";
	case QuestBook::Announce::All:
		return "all";
	case QuestBook::Announce::Level:
		break;
	}
	return "level";
}

QuestBook::Announce announceFrom(const QString &id, QuestBook::Announce fallback)
{
	if (id == QLatin1String("none"))
		return QuestBook::Announce::None;
	if (id == QLatin1String("level"))
		return QuestBook::Announce::Level;
	if (id == QLatin1String("all"))
		return QuestBook::Announce::All;
	return fallback;
}

int clampInt(const QJsonValue &v, int fallback, int low, int high)
{
	return std::clamp(v.isDouble() ? static_cast<int>(std::clamp(v.toDouble(), -1e9, 1e9)) : fallback, low, high);
}

} // namespace

namespace Quests {

QString kindId(Kind kind)
{
	return QLatin1String(kKindIds[static_cast<int>(kind)]);
}

std::optional<Kind> kindFromId(const QString &id)
{
	for (Kind k : allKinds()) {
		if (kindId(k) == id)
			return k;
	}
	return std::nullopt;
}

const QList<Kind> &allKinds()
{
	static const QList<Kind> kinds{Kind::Messages, Kind::Minutes, Kind::Dance,  Kind::Look,      Kind::Sound,
				       Kind::Tts,      Kind::Spend,   Kind::Raffle, Kind::Adventure, Kind::Boss,
				       Kind::Support,  Kind::Follow,  Kind::Sub};
	return kinds;
}

bool isBonus(Kind kind)
{
	return kind == Kind::Boss || kind == Kind::Support || kind == Kind::Follow || kind == Kind::Sub;
}

qint64 xpForLevel(int level)
{
	const qint64 l = std::max(1, level);
	return 25 * l * (l - 1);
}

int levelFor(qint64 xp)
{
	int level = 1;
	while (level < 1000 && xpForLevel(level + 1) <= xp)
		++level;
	return level;
}

} // namespace Quests

QuestBook::QuestBook(const QString &path, Chooser choose, Clock clock)
	: m_path(path),
	  m_choose(std::move(choose)),
	  m_clock(std::move(clock))
{
	if (!m_choose)
		m_choose = [](int n) {
			return static_cast<int>(QRandomGenerator::global()->bounded(n));
		};
	if (!m_clock)
		m_clock = []() {
			return QDateTime::currentMSecsSinceEpoch();
		};
	load();
}

QuestBook::~QuestBook()
{
	flush();
}

void QuestBook::flush()
{
	if (m_dirty)
		save();
}

QList<QuestDef> QuestBook::defaults()
{
	using Quests::Kind;
	/* {id, kind, goal, points, xp}: a live of an hour or so finishes
	 * about three of them. */
	return {
		{QStringLiteral("chat10"), Kind::Messages, 10, 20, 10, true, QString()},
		{QStringLiteral("chat30"), Kind::Messages, 30, 50, 25, true, QString()},
		{QStringLiteral("stay15"), Kind::Minutes, 15, 20, 10, true, QString()},
		{QStringLiteral("stay45"), Kind::Minutes, 45, 60, 30, true, QString()},
		{QStringLiteral("dance3"), Kind::Dance, 3, 15, 10, true, QString()},
		{QStringLiteral("look1"), Kind::Look, 1, 10, 5, true, QString()},
		{QStringLiteral("sound1"), Kind::Sound, 1, 20, 10, true, QString()},
		{QStringLiteral("spend100"), Kind::Spend, 100, 30, 15, true, QString()},
		{QStringLiteral("raffle1"), Kind::Raffle, 1, 15, 10, true, QString()},
		{QStringLiteral("adventure1"), Kind::Adventure, 1, 20, 10, true, QString()},
		{QStringLiteral("boss1"), Kind::Boss, 1, 50, 25, true, QString()},
		{QStringLiteral("support1"), Kind::Support, 1, 100, 50, true, QString()},
		{QStringLiteral("follow1"), Kind::Follow, 1, 50, 25, true, QString()},
		{QStringLiteral("sub1"), Kind::Sub, 1, 150, 75, true, QString()},
	};
}

QJsonObject QuestBook::toJson(const QuestDef &def)
{
	QJsonObject o{{QStringLiteral("id"), def.id},      {QStringLiteral("kind"), Quests::kindId(def.kind)},
		      {QStringLiteral("goal"), def.goal},  {QStringLiteral("points"), def.points},
		      {QStringLiteral("xp"), def.xp},      {QStringLiteral("enabled"), def.enabled},
		      {QStringLiteral("title"), def.title}};
	return o;
}

std::optional<QuestDef> QuestBook::fromJson(const QJsonObject &obj)
{
	const std::optional<Quests::Kind> kind = Quests::kindFromId(obj.value(QStringLiteral("kind")).toString());
	if (!kind)
		return std::nullopt;
	QuestDef def;
	def.id = obj.value(QStringLiteral("id")).toString().trimmed();
	def.kind = *kind;
	def.goal = clampInt(obj.value(QStringLiteral("goal")), 1, 1, kMaxGoal);
	def.points = clampInt(obj.value(QStringLiteral("points")), 0, 0, kMaxPoints);
	def.xp = clampInt(obj.value(QStringLiteral("xp")), 0, 0, kMaxXp);
	def.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
	def.title = obj.value(QStringLiteral("title")).toString().simplified().left(kMaxTitle);
	return def;
}

void QuestBook::load()
{
	const QJsonObject raw = BotData::readJsonFile(m_path);
	m_enabled = raw.value(QStringLiteral("enabled")).toBool(true);
	m_perViewer = clampInt(raw.value(QStringLiteral("perViewer")), 3, 1, kMaxPerViewer);
	m_whisper = raw.value(QStringLiteral("whisper")).toBool(true);
	m_showLevel = raw.value(QStringLiteral("showLevel")).toBool(true);
	m_announce = announceFrom(raw.value(QStringLiteral("announce")).toString(), Announce::Level);
	m_list.clear();
	if (raw.contains(QStringLiteral("list"))) {
		for (const QJsonValue v : raw.value(QStringLiteral("list")).toArray()) {
			if (const std::optional<QuestDef> def = fromJson(v.toObject()); def && !def->id.isEmpty())
				m_list.append(*def);
		}
	} else {
		m_list = defaults();
	}
	const QJsonObject adventure = raw.value(QStringLiteral("adventure")).toObject();
	m_adventureEnabled = adventure.value(QStringLiteral("enabled")).toBool(true);
	m_adventureCost = clampInt(adventure.value(QStringLiteral("cost")), 50, 0, kMaxPoints);
	m_adventureMinutes = clampInt(adventure.value(QStringLiteral("minutes")), 3, 1, kMaxAdventureMinutes);
	m_outcomes.clear();
	int weights = 0;
	for (const QJsonValue v : adventure.value(QStringLiteral("outcomes")).toArray()) {
		if (m_outcomes.size() >= kMaxOutcomes)
			break;
		const AdventureOutcome o = outcomeFromJson(v.toObject());
		if (!o.id.isEmpty()) {
			m_outcomes.append(o);
			weights += o.weight;
		}
	}
	if (weights <= 0)
		m_outcomes = defaultOutcomes();
	m_away.clear();
	const QJsonObject away = raw.value(QStringLiteral("away")).toObject();
	for (auto it = away.begin(); it != away.end(); ++it) {
		const QJsonObject o = it.value().toObject();
		m_away.insert(it.key().toLower(), {static_cast<qint64>(o.value(QStringLiteral("until")).toDouble()),
						   o.value(QStringLiteral("tag")).toString()});
	}
	m_session = raw.value(QStringLiteral("session")).toString();
	m_sessionStart = static_cast<qint64>(raw.value(QStringLiteral("sessionStart")).toDouble());
	m_lastLive = static_cast<qint64>(raw.value(QStringLiteral("lastLive")).toDouble());
	m_viewers.clear();
	const QJsonObject viewers = raw.value(QStringLiteral("viewers")).toObject();
	for (auto it = viewers.begin(); it != viewers.end(); ++it) {
		const QJsonObject o = it.value().toObject();
		Progress p;
		p.xp = std::max<qint64>(0, static_cast<qint64>(o.value(QStringLiteral("xp")).toDouble()));
		p.session = o.value(QStringLiteral("session")).toString();
		for (const QJsonValue q : o.value(QStringLiteral("quests")).toArray())
			p.quests.append(q.toString());
		const QJsonObject counts = o.value(QStringLiteral("counts")).toObject();
		for (auto c = counts.begin(); c != counts.end(); ++c)
			p.counts.insert(c.key(), c.value().toInt());
		for (const QJsonValue d : o.value(QStringLiteral("done")).toArray())
			p.done.append(d.toString());
		m_viewers.insert(it.key().toLower(), p);
	}
}

void QuestBook::save() const
{
	m_dirty = false;
	QJsonObject viewers;
	for (auto it = m_viewers.constBegin(); it != m_viewers.constEnd(); ++it) {
		const Progress &p = it.value();
		QJsonObject counts;
		for (auto c = p.counts.constBegin(); c != p.counts.constEnd(); ++c)
			counts.insert(c.key(), c.value());
		viewers.insert(it.key(), QJsonObject{{QStringLiteral("xp"), static_cast<double>(p.xp)},
						     {QStringLiteral("session"), p.session},
						     {QStringLiteral("quests"), QJsonArray::fromStringList(p.quests)},
						     {QStringLiteral("counts"), counts},
						     {QStringLiteral("done"), QJsonArray::fromStringList(p.done)}});
	}
	QJsonObject root = settingsJson();
	root.insert(QStringLiteral("session"), m_session);
	root.insert(QStringLiteral("sessionStart"), static_cast<double>(m_sessionStart));
	root.insert(QStringLiteral("lastLive"), static_cast<double>(m_lastLive));
	root.insert(QStringLiteral("viewers"), viewers);
	root.insert(QStringLiteral("adventure"), adventureJson());
	QJsonObject away;
	for (auto it = m_away.constBegin(); it != m_away.constEnd(); ++it)
		away.insert(it.key(), QJsonObject{{QStringLiteral("until"), static_cast<double>(it->until)},
						  {QStringLiteral("tag"), it->tag}});
	root.insert(QStringLiteral("away"), away);
	BotData::writeJsonFile(m_path, root);
}

QJsonObject QuestBook::settingsJson() const
{
	QJsonArray list;
	for (const QuestDef &def : m_list)
		list.append(toJson(def));
	return QJsonObject{{QStringLiteral("enabled"), m_enabled},
			   {QStringLiteral("perViewer"), m_perViewer},
			   {QStringLiteral("whisper"), m_whisper},
			   {QStringLiteral("showLevel"), m_showLevel},
			   {QStringLiteral("announce"), QLatin1String(announceId(m_announce))},
			   {QStringLiteral("list"), list}};
}

QString QuestBook::applySettings(const QJsonObject &settings)
{
	QList<QuestDef> list = m_list;
	if (settings.contains(QStringLiteral("list"))) {
		const QJsonArray raw = settings.value(QStringLiteral("list")).toArray();
		if (raw.size() > kMaxQuests)
			return QStringLiteral("Texuguito.Parade.QuestErrorTooMany");
		list.clear();
		QSet<QString> ids;
		for (const QJsonValue v : raw) {
			std::optional<QuestDef> def = fromJson(v.toObject());
			if (!def)
				return QStringLiteral("Texuguito.Parade.QuestErrorBad");
			/* A new row (or a copy) gets its own id. */
			if (def->id.isEmpty() || ids.contains(def->id))
				def->id = QStringLiteral("q") + QUuid::createUuid().toString(QUuid::Id128).left(8);
			ids.insert(def->id);
			list.append(*def);
		}
	}
	m_list = list;
	m_enabled = settings.value(QStringLiteral("enabled")).toBool(m_enabled);
	m_perViewer = clampInt(settings.value(QStringLiteral("perViewer")), m_perViewer, 1, kMaxPerViewer);
	m_whisper = settings.value(QStringLiteral("whisper")).toBool(m_whisper);
	m_showLevel = settings.value(QStringLiteral("showLevel")).toBool(m_showLevel);
	m_announce = announceFrom(settings.value(QStringLiteral("announce")).toString(), m_announce);
	save();
	return QString();
}

void QuestBook::restoreDefaults()
{
	m_list = defaults();
	save();
}

QList<AdventureOutcome> QuestBook::defaultOutcomes()
{
	/* {id, weight, points, xp}: with the default cost of 50 a viewer gets
	 * about the points back on average, and the XP on top. */
	return {
		{QStringLiteral("nothing"), 35, 0, 5, QString()},
		{QStringLiteral("coins"), 40, 50, 10, QString()},
		{QStringLiteral("treasure"), 20, 100, 15, QString()},
		{QStringLiteral("legendary"), 5, 200, 30, QString()},
	};
}

QJsonObject QuestBook::toJson(const AdventureOutcome &outcome)
{
	return QJsonObject{{QStringLiteral("id"), outcome.id},
			   {QStringLiteral("weight"), outcome.weight},
			   {QStringLiteral("points"), outcome.points},
			   {QStringLiteral("xp"), outcome.xp},
			   {QStringLiteral("title"), outcome.title}};
}

AdventureOutcome QuestBook::outcomeFromJson(const QJsonObject &obj)
{
	AdventureOutcome o;
	o.id = obj.value(QStringLiteral("id")).toString().trimmed();
	o.weight = clampInt(obj.value(QStringLiteral("weight")), 1, 0, kMaxWeight);
	o.points = clampInt(obj.value(QStringLiteral("points")), 0, 0, kMaxPoints);
	o.xp = clampInt(obj.value(QStringLiteral("xp")), 0, 0, kMaxXp);
	o.title = obj.value(QStringLiteral("title")).toString().simplified().left(kMaxTitle);
	return o;
}

QJsonObject QuestBook::adventureJson() const
{
	QJsonArray outcomes;
	for (const AdventureOutcome &o : m_outcomes)
		outcomes.append(toJson(o));
	return QJsonObject{{QStringLiteral("enabled"), m_adventureEnabled},
			   {QStringLiteral("cost"), m_adventureCost},
			   {QStringLiteral("minutes"), m_adventureMinutes},
			   {QStringLiteral("outcomes"), outcomes}};
}

QString QuestBook::applyAdventure(const QJsonObject &settings)
{
	QList<AdventureOutcome> outcomes = m_outcomes;
	if (settings.contains(QStringLiteral("outcomes"))) {
		const QJsonArray raw = settings.value(QStringLiteral("outcomes")).toArray();
		if (raw.size() > kMaxOutcomes)
			return QStringLiteral("Texuguito.Parade.AdventureErrorTooMany");
		outcomes.clear();
		QSet<QString> ids;
		int weights = 0;
		for (const QJsonValue v : raw) {
			AdventureOutcome o = outcomeFromJson(v.toObject());
			if (o.id.isEmpty() || ids.contains(o.id))
				o.id = QStringLiteral("a") + QUuid::createUuid().toString(QUuid::Id128).left(8);
			ids.insert(o.id);
			weights += o.weight;
			outcomes.append(o);
		}
		if (weights <= 0)
			return QStringLiteral("Texuguito.Parade.AdventureErrorNoChance");
	}
	m_outcomes = outcomes;
	m_adventureEnabled = settings.value(QStringLiteral("enabled")).toBool(m_adventureEnabled);
	m_adventureCost = clampInt(settings.value(QStringLiteral("cost")), m_adventureCost, 0, kMaxPoints);
	m_adventureMinutes =
		clampInt(settings.value(QStringLiteral("minutes")), m_adventureMinutes, 1, kMaxAdventureMinutes);
	save();
	return QString();
}

void QuestBook::restoreAdventureDefaults()
{
	m_outcomes = defaultOutcomes();
	m_adventureCost = 50;
	m_adventureMinutes = 3;
	save();
}

qint64 QuestBook::awayFor(const QString &key) const
{
	const auto it = m_away.constFind(key.toLower());
	return it == m_away.constEnd() ? 0 : std::max<qint64>(0, it->until - m_clock());
}

bool QuestBook::startAdventure(const QString &key, const QString &tag)
{
	if (!m_adventureEnabled || key.isEmpty() || m_away.contains(key.toLower()))
		return false;
	m_away.insert(key.toLower(), {m_clock() + m_adventureMinutes * 60000LL, tag});
	save();
	return true;
}

QList<AdventureResult> QuestBook::finishAdventures()
{
	QList<AdventureResult> results;
	const qint64 now = m_clock();
	int weights = 0;
	for (const AdventureOutcome &o : std::as_const(m_outcomes))
		weights += o.weight;
	for (auto it = m_away.begin(); it != m_away.end();) {
		if (it->until > now) {
			++it;
			continue;
		}
		/* Turned off in the middle: they still come back with a prize. */
		AdventureOutcome outcome;
		if (weights > 0) {
			int roll = std::clamp(m_choose(weights), 0, weights - 1);
			for (const AdventureOutcome &o : std::as_const(m_outcomes)) {
				if (roll < o.weight) {
					outcome = o;
					break;
				}
				roll -= o.weight;
			}
		}
		const XpGain gain = addXp(it.key(), outcome.xp);
		results.append({it.key(), it->tag, outcome, gain.xp, gain.level, gain.levelUp});
		it = m_away.erase(it);
	}
	if (!results.isEmpty())
		save();
	return results;
}

void QuestBook::newSession()
{
	const qint64 now = m_clock();
	m_session = QString::number(now);
	m_sessionStart = now;
}

void QuestBook::ensureSession()
{
	const qint64 now = m_clock();
	if (m_session.isEmpty() ||
	    (!m_live && now - m_sessionStart > kOfflineSessionMs && now - m_lastLive > kRejoinMs))
		newSession();
}

void QuestBook::setLive(bool live)
{
	const qint64 now = m_clock();
	if (live) {
		/* A live that dropped (or OBS that restarted) a moment ago goes on. */
		if (!m_live && (m_session.isEmpty() || now - m_lastLive > kRejoinMs)) {
			newSession();
			m_lastLive = now;
			m_live = true;
			save();
			return;
		}
		m_live = true;
		m_lastLive = now;
	} else if (m_live) {
		m_live = false;
		m_lastLive = now;
		save();
	}
}

QuestBook::Progress &QuestBook::viewer(const QString &key)
{
	Progress &p = m_viewers[key.toLower()];
	if (p.session != m_session) {
		p.session = m_session;
		p.quests.clear();
		p.counts.clear();
		p.done.clear();
	}
	return p;
}

const QuestDef *QuestBook::find(const QString &id) const
{
	for (const QuestDef &def : m_list) {
		if (def.id == id)
			return &def;
	}
	return nullptr;
}

bool QuestBook::draw(Progress &p)
{
	bool changed = false;
	/* Quests the streamer removed, turned off or made bonus go away;
	 * finished ones stay, so finishing does not draw a new one. */
	for (qsizetype i = p.quests.size() - 1; i >= 0; --i) {
		const QuestDef *def = find(p.quests[i]);
		if (!p.done.contains(p.quests[i]) && (!def || !def->enabled || Quests::isBonus(def->kind))) {
			p.quests.removeAt(i);
			changed = true;
		}
	}
	QList<const QuestDef *> pool;
	for (const QuestDef &def : m_list) {
		if (def.enabled && !Quests::isBonus(def.kind) && !p.quests.contains(def.id))
			pool.append(&def);
	}
	while (p.quests.size() < m_perViewer && !pool.isEmpty()) {
		const int index =
			std::clamp(m_choose(static_cast<int>(pool.size())), 0, static_cast<int>(pool.size()) - 1);
		p.quests.append(pool.takeAt(index)->id);
		changed = true;
	}
	return changed;
}

QList<QuestReward> QuestBook::progress(const QString &key, Quests::Kind kind, int amount)
{
	QList<QuestReward> rewards;
	if (!m_enabled || amount <= 0 || key.isEmpty())
		return rewards;
	ensureSession();
	Progress &p = viewer(key);
	bool changed = draw(p);
	for (const QuestDef &def : m_list) {
		if (!def.enabled || def.kind != kind || p.done.contains(def.id))
			continue;
		if (!Quests::isBonus(kind) && !p.quests.contains(def.id))
			continue;
		const int count = std::min(def.goal, p.counts.value(def.id) + amount);
		p.counts[def.id] = count;
		changed = true;
		if (count < def.goal)
			continue;
		const int before = Quests::levelFor(p.xp);
		p.done.append(def.id);
		p.xp += def.xp;
		const int after = Quests::levelFor(p.xp);
		rewards.append({key.toLower(), def, p.xp, after, after > before});
	}
	if (!rewards.isEmpty())
		save();
	else if (changed)
		m_dirty = true;
	return rewards;
}

QList<QuestEntry> QuestBook::questsFor(const QString &key)
{
	QList<QuestEntry> entries;
	ensureSession();
	Progress &p = viewer(key);
	if (draw(p))
		m_dirty = true;
	for (const QString &id : std::as_const(p.quests)) {
		if (const QuestDef *def = find(id))
			entries.append({*def, p.counts.value(id), p.done.contains(id)});
	}
	for (const QuestDef &def : m_list) {
		if (def.enabled && Quests::isBonus(def.kind))
			entries.append({def, p.counts.value(def.id), p.done.contains(def.id)});
	}
	return entries;
}

QuestBook::XpGain QuestBook::addXp(const QString &key, int xp)
{
	Progress &p = m_viewers[key.toLower()];
	const int before = Quests::levelFor(p.xp);
	p.xp += std::max(0, xp);
	m_dirty = true;
	const int after = Quests::levelFor(p.xp);
	return {p.xp, after, after > before};
}

qint64 QuestBook::xp(const QString &key) const
{
	return m_viewers.value(key.toLower()).xp;
}
