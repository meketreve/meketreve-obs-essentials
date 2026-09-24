/*
Meketreve OBS Essentials - tab layout store unit tests (developer tool)
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
#include <QTest>

class TestLayoutStore : public QObject {
	Q_OBJECT

private slots:
	void defaultsHaveMineLiveBuild()
	{
		const TabsConfig cfg = TabsConfig::defaults("state", QStringLiteral("Meu layout"));
		QCOMPARE(cfg.tabs.size(), 3);
		QCOMPARE(cfg.current, QStringLiteral("mine"));
		QCOMPARE(cfg.tabs[0].state, QByteArray("state"));
		QVERIFY(!cfg.tabs[0].isRemovable());
		QVERIFY(cfg.tabs[1].isFixed());
		QVERIFY(cfg.tabs[2].isFixed());
		QVERIFY(cfg.tabs[1].state.isEmpty());
	}

	void roundTrip()
	{
		TabsConfig cfg = TabsConfig::defaults(QByteArray("\x00\x01\xff", 3), QStringLiteral("Mine"));
		TabLayout custom;
		custom.id = cfg.newCustomId();
		custom.name = QStringLiteral("Jogo");
		custom.state = "abc";
		custom.previewShown = true;
		cfg.tabs.append(custom);
		cfg.current = custom.id;

		TabsConfig back;
		QVERIFY(TabsConfig::fromJson(cfg.toJson(), back));
		QCOMPARE(back.tabs.size(), 4);
		QCOMPARE(back.current, QStringLiteral("custom-1"));
		QCOMPARE(back.tabs[0].state, QByteArray("\x00\x01\xff", 3));
		QCOMPARE(back.tabs[3].name, QStringLiteral("Jogo"));
		QVERIFY(back.tabs[3].previewShown);
		QVERIFY(back.tabs[3].isRemovable());
		QCOMPARE(back.newCustomId(), QStringLiteral("custom-2"));
	}

	void withoutStates()
	{
		const TabsConfig cfg = TabsConfig::defaults("state", QStringLiteral("Mine"));
		const QJsonObject obj = cfg.toJson(false);
		for (const QJsonValue v : obj.value(QStringLiteral("tabs")).toArray())
			QVERIFY(!v.toObject().contains(QStringLiteral("state")));
	}

	void rejectsFutureAndMissingFormat()
	{
		TabsConfig out;
		QString error;
		QVERIFY(!TabsConfig::fromJson(QJsonObject{{QStringLiteral("format"), 99}}, out, &error));
		QVERIFY(error.contains(QStringLiteral("newer")));
		QVERIFY(!TabsConfig::fromJson(QJsonObject{}, out, &error));
	}

	void repairsBrokenFile()
	{
		/* Duplicated ids, a nameless custom tab and missing fixed tabs. */
		const QJsonObject obj{{QStringLiteral("format"), 1},
				      {QStringLiteral("current"), QStringLiteral("gone")},
				      {QStringLiteral("tabs"),
				       QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("x")}},
						  QJsonObject{{QStringLiteral("id"), QStringLiteral("x")}},
						  QJsonObject{{QStringLiteral("name"), QStringLiteral("no id")}}}}};
		TabsConfig out;
		QVERIFY(TabsConfig::fromJson(obj, out));
		QCOMPARE(out.tabs.size(), 3);
		QCOMPARE(out.tabs[0].name, QStringLiteral("x"));
		QVERIFY(out.indexOf(QStringLiteral("live")) >= 0);
		QVERIFY(out.indexOf(QStringLiteral("build")) >= 0);
		QCOMPARE(out.current, QStringLiteral("x"));
	}
};

QTEST_GUILESS_MAIN(TestLayoutStore)
#include "test-layout-store.moc"
