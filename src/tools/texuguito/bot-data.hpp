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

/* Texuguito's data, ported from texuguito-seu-bot-amigo (same JSON files, so
 * an existing data/ folder can be copied over): viewers.json, points.json and
 * custom_commands.json. Keys are lowercase usernames; viewers from platforms
 * other than Twitch get a "kick:", "yt:" or "tt:" prefix so names from
 * different platforms never merge. */

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace BotData {

/* Reads a JSON object file; a broken file is moved to <file>.corrupt and an
 * empty object returned, so a hand-edit typo never blocks startup. */
QJsonObject readJsonFile(const QString &path);
bool writeJsonFile(const QString &path, const QJsonObject &obj);

QString defaultColorFor(const QString &key);
std::optional<QString> validateColor(const QString &raw);
/* Hat/accessory: {valid, value}; value is empty for "nenhum"/"none". */
std::pair<bool, QString> validateHat(const QString &raw);
std::pair<bool, QString> validateAccessory(const QString &raw);
QString validateNick(const QString &raw); /* empty = invalid */
const QStringList &hats();
const QStringList &accessories();

} // namespace BotData

struct Viewer {
	QString cor;
	QString chapeu;
	QString acessorio;
	QString nick;
	QString exibicao; /* display name from chat, used when there is no nick */
	QString primeiraVez;
	QString ultimaVez;
};

class ViewerStore {
public:
	explicit ViewerStore(const QString &path);

	void load();
	void save() const;

	Viewer &getOrCreate(const QString &key, const QString &displayName = QString());
	bool contains(const QString &key) const { return m_viewers.contains(key.toLower()); }
	QStringList keys() const { return m_viewers.keys(); }
	void setColor(const QString &key, const QString &cor);
	void resetColor(const QString &key);
	void setHat(const QString &key, const QString &hat);
	void setAccessory(const QString &key, const QString &accessory);
	void setNick(const QString &key, const QString &nick);

private:
	void touch(Viewer &v) const;

	QString m_path;
	QHash<QString, Viewer> m_viewers;
};

class PointsStore {
public:
	explicit PointsStore(const QString &path);

	void load();
	void save() const;
	qint64 get(const QString &key) const { return m_points.value(key.toLower(), 0); }
	void add(const QString &key, qint64 amount) { addMany({key}, amount); }
	void addMany(const QSet<QString> &keys, qint64 amount);
	bool spend(const QString &key, qint64 amount);

private:
	QString m_path;
	QHash<QString, qint64> m_points;
};

class CustomCommandStore {
public:
	explicit CustomCommandStore(const QString &path);

	void load();
	void save() const;
	QString get(const QString &name) const { return m_commands.value(name.toLower()); }
	bool contains(const QString &name) const { return m_commands.contains(name.toLower()); }
	QStringList names() const;
	void set(const QString &name, const QString &response);
	bool remove(const QString &name);

private:
	QString m_path;
	QHash<QString, QString> m_commands;
};

/* One points raffle at a time: open it, viewers !entrar, a random one wins. */
class Raffle {
public:
	using Chooser = std::function<QString(const QStringList &)>;
	explicit Raffle(Chooser choose = Chooser());

	bool active() const { return m_active; }
	bool start(qint64 prize);
	bool join(const QString &key);
	/* Closes it and credits the winner; returns {winner key, prize}. */
	std::pair<QString, qint64> finish(PointsStore &points);

private:
	Chooser m_choose;
	bool m_active = false;
	qint64 m_prize = 0;
	QSet<QString> m_participants;
};
