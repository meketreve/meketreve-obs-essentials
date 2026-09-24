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

#include "layout-store.hpp"

#include <QJsonArray>

TabsConfig TabsConfig::defaults(const QByteArray &currentState, const QString &myLayoutName)
{
	TabsConfig cfg;
	TabLayout mine;
	mine.id = QStringLiteral("mine");
	mine.name = myLayoutName;
	mine.state = currentState;
	cfg.tabs.append(mine);

	TabLayout live;
	live.id = QStringLiteral("live");
	cfg.tabs.append(live);

	TabLayout build;
	build.id = QStringLiteral("build");
	cfg.tabs.append(build);

	cfg.current = mine.id;
	return cfg;
}

qsizetype TabsConfig::indexOf(const QString &id) const
{
	for (qsizetype i = 0; i < tabs.size(); i++) {
		if (tabs[i].id == id)
			return i;
	}
	return -1;
}

QString TabsConfig::newCustomId() const
{
	for (int n = 1;; n++) {
		const QString id = QStringLiteral("custom-%1").arg(n);
		if (indexOf(id) < 0)
			return id;
	}
}

QJsonObject TabsConfig::toJson(bool withStates) const
{
	QJsonArray arr;
	for (const TabLayout &t : tabs) {
		QJsonObject o{{QStringLiteral("id"), t.id}};
		if (!t.name.isEmpty())
			o.insert(QStringLiteral("name"), t.name);
		if (withStates && !t.state.isEmpty()) {
			o.insert(QStringLiteral("state"), QString::fromLatin1(t.state.toBase64()));
			o.insert(QStringLiteral("previewShown"), t.previewShown);
		}
		arr.append(o);
	}
	return QJsonObject{{QStringLiteral("format"), kFormat},
			   {QStringLiteral("current"), current},
			   {QStringLiteral("tabs"), arr}};
}

bool TabsConfig::fromJson(const QJsonObject &obj, TabsConfig &out, QString *error)
{
	const auto fail = [error](const char *why) {
		if (error)
			*error = QString::fromLatin1(why);
		return false;
	};

	const int format = obj.value(QStringLiteral("format")).toInt(0);
	if (format < 1)
		return fail("missing format");
	if (format > kFormat)
		return fail("made by a newer version of the plugin");

	TabsConfig cfg;
	for (const QJsonValue v : obj.value(QStringLiteral("tabs")).toArray()) {
		const QJsonObject o = v.toObject();
		TabLayout t;
		t.id = o.value(QStringLiteral("id")).toString();
		if (t.id.isEmpty() || cfg.indexOf(t.id) >= 0)
			continue;
		t.name = o.value(QStringLiteral("name")).toString();
		if (!t.isFixed() && t.name.isEmpty())
			t.name = t.id;
		t.state = QByteArray::fromBase64(o.value(QStringLiteral("state")).toString().toLatin1());
		t.previewShown = o.value(QStringLiteral("previewShown")).toBool();
		cfg.tabs.append(t);
	}

	/* Live and Build are always there, even if an edited file lost them. */
	for (const char *fixed : {"live", "build"}) {
		if (cfg.indexOf(QString::fromLatin1(fixed)) < 0) {
			TabLayout t;
			t.id = QString::fromLatin1(fixed);
			cfg.tabs.append(t);
		}
	}

	cfg.current = obj.value(QStringLiteral("current")).toString();
	if (cfg.indexOf(cfg.current) < 0)
		cfg.current = cfg.tabs.first().id;
	out = cfg;
	return true;
}
