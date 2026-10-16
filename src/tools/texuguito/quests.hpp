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

/* Quests for the parade's viewers: each one gets a few missions drawn from
 * the streamer's list every live (send messages, stay a while, dance...),
 * and finishing one gives points and XP; XP makes the avatar's level. Some
 * missions are bonus ones (follow, sub, support): open to everyone, never
 * drawn, since not everyone can do them. Adventures: a viewer pays points
 * to send their avatar away for a few minutes, and it comes back with a
 * prize drawn by weight. Saved in quests.json: the list, the settings, each
 * viewer's progress in the current live and who is away. No OBS
 * dependency, so it runs in unit tests. */

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Quests {

enum class Kind { Messages, Minutes, Dance, Look, Sound, Tts, Spend, Raffle, Adventure, Boss, Support, Follow, Sub };

QString kindId(Kind kind);
std::optional<Kind> kindFromId(const QString &id);
const QList<Kind> &allKinds();
/* Boss, follow, sub and support: open to everyone, never drawn (a boss
 * only comes when the streamer calls it). */
bool isBonus(Kind kind);

/* Total XP that reaches a level (level 1 = 0, 2 = 50, 3 = 150, 4 = 300...):
 * each level asks 50 XP more than the last. */
qint64 xpForLevel(int level);
int levelFor(qint64 xp);

} // namespace Quests

struct QuestDef {
	QString id;
	Quests::Kind kind = Quests::Kind::Messages;
	int goal = 1;
	int points = 0;
	int xp = 0;
	bool enabled = true;
	QString title; /* empty = the kind's text with the goal */
};

/* A quest finished just now. */
struct QuestReward {
	QString key;
	QuestDef quest;
	qint64 xp = 0; /* total after this one */
	int level = 1;
	bool levelUp = false;
};

/* A prize an adventure can bring back; the chance is weight over the sum
 * of the weights. */
struct AdventureOutcome {
	QString id;
	int weight = 1;
	int points = 0;
	int xp = 0;
	QString title; /* empty = the default text of the id */
};

/* An adventure that ended just now. */
struct AdventureResult {
	QString key;
	QString tag; /* what startAdventure got: where to say it */
	AdventureOutcome outcome;
	qint64 xp = 0; /* total after this one */
	int level = 1;
	bool levelUp = false;
};

/* One quest of a viewer, as !quests shows it. */
struct QuestEntry {
	QuestDef quest;
	int count = 0;
	bool done = false;
};

class QuestBook {
public:
	static constexpr int kMaxQuests = 30;
	static constexpr int kMaxPerViewer = 5;
	static constexpr int kMaxGoal = 100000;
	static constexpr int kMaxPoints = 100000;
	static constexpr int kMaxXp = 10000;
	static constexpr int kMaxTitle = 60;
	/* A live that drops and comes back within this keeps its quests. */
	static constexpr qint64 kRejoinMs = 30 * 60000LL;
	/* Without lives (bot used off stream), the quests renew after this. */
	static constexpr qint64 kOfflineSessionMs = 12 * 3600000LL;
	static constexpr int kMaxOutcomes = 10;
	static constexpr int kMaxWeight = 1000;
	static constexpr int kMaxAdventureMinutes = 60;

	enum class Announce { None, Level, All };

	/* Random index in [0, n): the quests a viewer gets. Clock: ms since
	 * the epoch. Both have real defaults; tests pass their own. */
	using Chooser = std::function<int(int n)>;
	using Clock = std::function<qint64()>;
	explicit QuestBook(const QString &path, Chooser choose = Chooser(), Clock clock = Clock());
	~QuestBook();

	void load();
	void save() const;
	/* Counts that moved without finishing anything are saved here (the
	 * bot calls it every few seconds), not on every chat line; a finished
	 * quest and the settings are saved at once. */
	void flush();

	bool enabled() const { return m_enabled; }
	int perViewer() const { return m_perViewer; }
	bool whisper() const { return m_whisper; }
	bool showLevel() const { return m_showLevel; }
	Announce announce() const { return m_announce; }
	const QList<QuestDef> &list() const { return m_list; }
	static QList<QuestDef> defaults();

	/* The settings and the list, for the web panel; applySettings takes
	 * the same shape (what is left out keeps its value) and answers an
	 * error key ("Texuguito.Parade.QuestError*") or empty. */
	QJsonObject settingsJson() const;
	QString applySettings(const QJsonObject &settings);
	void restoreDefaults();

	bool adventureEnabled() const { return m_adventureEnabled; }
	int adventureCost() const { return m_adventureCost; }
	int adventureMinutes() const { return m_adventureMinutes; }
	const QList<AdventureOutcome> &outcomes() const { return m_outcomes; }
	static QList<AdventureOutcome> defaultOutcomes();
	/* The adventure settings, as the panel edits them; applyAdventure
	 * works like applySettings. */
	QJsonObject adventureJson() const;
	QString applyAdventure(const QJsonObject &settings);
	void restoreAdventureDefaults();
	/* ms until key's avatar comes back; 0 = not away. */
	qint64 awayFor(const QString &key) const;
	/* Sends key away (the caller already took the cost); tag comes back
	 * in the result. False when adventures are off or key is away. */
	bool startAdventure(const QString &key, const QString &tag);
	/* The adventures whose time is up: the prize drawn, the XP paid (the
	 * points are the caller's). */
	QList<AdventureResult> finishAdventures();

	/* Adds XP outside the quests (the boss): the total and the level after
	 * it, and if it went up. Saved with the next flush(). */
	struct XpGain {
		qint64 xp = 0;
		int level = 1;
		bool levelUp = false;
	};
	XpGain addXp(const QString &key, int xp);

	/* For tests: another clock. */
	void setClock(Clock clock) { m_clock = std::move(clock); }

	/* The stream went live or stopped: a new live gets new quests. */
	void setLive(bool live);
	QString session() const { return m_session; }

	/* key did amount of kind: answers the quests it finished. Nothing
	 * while quests are off. */
	QList<QuestReward> progress(const QString &key, Quests::Kind kind, int amount = 1);
	/* The viewer's quests in this live (drawn now if they have none yet),
	 * then the bonus ones. */
	QList<QuestEntry> questsFor(const QString &key);
	qint64 xp(const QString &key) const;
	int level(const QString &key) const { return Quests::levelFor(xp(key)); }

	static QJsonObject toJson(const QuestDef &def);
	static std::optional<QuestDef> fromJson(const QJsonObject &obj);
	static QJsonObject toJson(const AdventureOutcome &outcome);
	static AdventureOutcome outcomeFromJson(const QJsonObject &obj);

private:
	struct Progress {
		qint64 xp = 0;
		QString session;
		QStringList quests;         /* drawn for this live */
		QHash<QString, int> counts; /* by quest id */
		QStringList done;
	};

	/* Starts a new session when the one there is old (see kOfflineSessionMs). */
	void ensureSession();
	void newSession();
	Progress &viewer(const QString &key);
	/* Fills p up to perViewer quests; true when it changed. */
	bool draw(Progress &p);
	const QuestDef *find(const QString &id) const;

	QString m_path;
	Chooser m_choose;
	Clock m_clock;
	bool m_enabled = true;
	int m_perViewer = 3;
	bool m_whisper = true;
	bool m_showLevel = true;
	Announce m_announce = Announce::Level;
	QList<QuestDef> m_list;
	QString m_session;
	qint64 m_sessionStart = 0;
	bool m_live = false;
	/* The last moment the stream was seen live (saved, so OBS closing in
	 * the middle of a live does not start a new one). */
	qint64 m_lastLive = 0;
	QHash<QString, Progress> m_viewers;
	bool m_adventureEnabled = true;
	int m_adventureCost = 50;
	int m_adventureMinutes = 3;
	QList<AdventureOutcome> m_outcomes;
	struct Away {
		qint64 until = 0;
		QString tag;
	};
	QHash<QString, Away> m_away;
	mutable bool m_dirty = false;
};
