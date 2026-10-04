/*
Meketreve OBS Essentials - Alerts
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

#include "alert-logic.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>

/* The events overlay at /eventos: a list of the latest events, or with
 * "?mostrar=<label>" one line such as "Last sub: Someone". It keeps what
 * happened on disk so the labels survive an OBS restart. Pure logic, so the
 * tests run without OBS. */
namespace EventsOverlay {

/* ultimo-follow, ultimo-sub, ..., top-doador, top-bits: the "mostrar" values. */
QStringList labelKinds();

QJsonObject defaults(const Alerts::TextLookup &text);
/* Fills what is missing and clamps what is out of range. */
QJsonObject normalize(const QJsonObject &stored, const Alerts::TextLookup &text);

struct Entry {
	QString id;
	QString platform;
	QString type;
	QString name;
	QString amount; /* as shown: "5", "R$ 10,00" */
	double value = 0;
	QString message;
	QString text; /* the Activity panel's line, in the OBS language */
	qint64 at = 0;
};

Entry entryFrom(const Alerts::Event &event, const QString &text, const QString &id, qint64 nowMs);
QJsonObject toJson(const Entry &entry);

class History {
public:
	static constexpr int kKeep = 100;

	void add(const Entry &entry);
	/* Starts the "top" labels over (a new live); the latest ones stay. */
	void resetTop();
	/* Newest first. */
	QJsonArray recent(int count) const;
	/* label kind -> {name, amount, platform}; kinds with nothing yet are left
	 * out. */
	QJsonObject labels() const;

	QJsonObject save() const;
	void load(const QJsonObject &stored);

private:
	struct Total {
		QString name;
		QString platform;
		QString prefix; /* "R$ " from "R$ 10,00": money in the same format */
		double sum = 0;
	};
	static QString money(const Total &total);

	QList<Entry> m_entries; /* oldest first */
	/* The latest one per label kind, kept apart so a rare kind (a raid)
	 * is not pushed out of the list by many follows. */
	QHash<QString, Entry> m_last;
	QHash<QString, Total> m_donors;
	QHash<QString, Total> m_bits;
};

} // namespace EventsOverlay
