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

/* The collective quest: a boss shows up on the parade with a life bar and
 * the whole chat beats it together, with chat lines, !attack and bigger
 * hits from support, subs and follows. Beaten in time, everyone who hit it
 * gets points and XP (the one who hit hardest, a bonus); otherwise it gets
 * away. The settings are saved in boss.json; a fight lives only in memory.
 * No OBS dependency, so it runs in unit tests. */

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <optional>

class BossFight {
public:
	enum class Hit { Message, Attack, Support, Sub, Follow };

	static constexpr int kMaxHp = 1000000;
	static constexpr int kMaxMinutes = 30;
	static constexpr int kMaxAutoMinutes = 240;
	static constexpr int kMaxPoints = 100000;
	static constexpr int kMaxXp = 10000;
	static constexpr int kMaxName = 40;

	/* How hard each kind of hit is, and how long a viewer waits between
	 * two of the same kind (chat lines and !attack; the rest never wait). */
	static int damage(Hit hit);
	static qint64 waitMs(Hit hit);

	/* ms since the epoch; tests pass their own. */
	using Clock = std::function<qint64()>;
	explicit BossFight(const QString &path, Clock clock = Clock());

	void load();
	void save() const;

	bool enabled() const { return m_enabled; }
	/* Empty = the default name, in the stream language. */
	QString name() const { return m_name; }
	int baseHp() const { return m_baseHp; }
	int hpPerViewer() const { return m_hpPerViewer; }
	int minutes() const { return m_minutes; }
	/* A fight on its own every this many minutes of live; 0 = only by hand. */
	int autoMinutes() const { return m_autoMinutes; }
	int points() const { return m_points; }
	int xp() const { return m_xp; }
	int mvpPoints() const { return m_mvpPoints; }

	/* The settings, for the web panel; applySettings takes the same shape
	 * (what is left out keeps its value). */
	QJsonObject settingsJson() const;
	void applySettings(const QJsonObject &settings);
	void restoreDefaults();

	bool active() const { return m_active; }
	/* A new fight, its life sized for that many viewers present. False
	 * when bosses are off or one is already on. */
	bool start(int viewers);
	/* key hits the boss: the damage done (0 = no fight, or key must wait). */
	int hit(const QString &key, Hit hit);
	int hp() const { return m_hp; }
	int maxHp() const { return m_maxHp; }
	qint64 remainingMs() const;

	struct Result {
		bool defeated = false;
		int hpLeft = 0;
		/* Who hit it and how hard, the hardest first. */
		QList<QPair<QString, int>> damage;
	};
	/* The end of the fight, once: beaten, or the time is up. */
	std::optional<Result> finish();
	/* True when a fight should start on its own: live, none on, and
	 * autoMinutes since the last one ended (or since the live began). */
	bool autoDue(bool live);

	/* For tests: another clock. */
	void setClock(Clock clock) { m_clock = std::move(clock); }

private:
	QString m_path;
	Clock m_clock;
	bool m_enabled = true;
	QString m_name;
	int m_baseHp = 100;
	int m_hpPerViewer = 30;
	int m_minutes = 5;
	int m_autoMinutes = 0;
	int m_points = 100;
	int m_xp = 30;
	int m_mvpPoints = 50;

	bool m_active = false;
	int m_hp = 0;
	int m_maxHp = 0;
	qint64 m_until = 0;
	QHash<QString, int> m_damage;
	QHash<QString, qint64> m_lastHit; /* "<kind>:<key>" */
	qint64 m_autoFrom = 0;            /* 0 = not counting (off stream) */
};
