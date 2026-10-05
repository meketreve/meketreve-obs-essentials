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
	void defaultsAreLiveAndBuild()
	{
		const TabsConfig cfg = TabsConfig::defaults();
		QCOMPARE(cfg.tabs.size(), 2);
		QCOMPARE(cfg.current, QStringLiteral("live"));
		QVERIFY(cfg.tabs[0].isFixed());
		QVERIFY(cfg.tabs[1].isFixed());
		QVERIFY(!cfg.tabs[0].isRemovable());
		QVERIFY(cfg.tabs[0].state.isEmpty());

		TabLayout custom;
		custom.id = QStringLiteral("custom-1");
		QVERIFY(custom.isRemovable());
	}

	void roundTrip()
	{
		TabsConfig cfg = TabsConfig::defaults();
		cfg.tabs[0].state = QByteArray("\x00\x01\xff", 3);
		TabLayout custom;
		custom.id = cfg.newCustomId();
		custom.name = QStringLiteral("Jogo");
		custom.state = "abc";
		custom.previewShown = true;
		custom.window = QSize(1440, 863);
		custom.sizes.insert(QStringLiteral("scenesDock"), QSize(290, 224));
		cfg.tabs.append(custom);
		cfg.current = custom.id;

		TabsConfig back;
		QVERIFY(TabsConfig::fromJson(cfg.toJson(), back));
		QCOMPARE(back.tabs.size(), 3);
		QCOMPARE(back.current, QStringLiteral("custom-1"));
		QCOMPARE(back.tabs[0].state, QByteArray("\x00\x01\xff", 3));
		QCOMPARE(back.tabs[2].name, QStringLiteral("Jogo"));
		QVERIFY(back.tabs[2].previewShown);
		QCOMPARE(back.tabs[2].window, QSize(1440, 863));
		QCOMPARE(back.tabs[2].sizes.value(QStringLiteral("scenesDock")), QSize(290, 224));
		QVERIFY(!back.tabs[0].window.isValid());
		QVERIFY(back.tabs[2].isRemovable());
		QCOMPARE(back.newCustomId(), QStringLiteral("custom-2"));
	}

	void withoutStates()
	{
		TabsConfig cfg = TabsConfig::defaults();
		cfg.tabs[0].state = "state";
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
