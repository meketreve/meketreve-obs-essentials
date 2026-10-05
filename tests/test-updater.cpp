/*
Meketreve OBS Essentials - updater unit tests (developer tool)
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
#include "update-logic.hpp"

#include <QJsonArray>
#include <QTest>

using namespace UpdateLogic;

class TestUpdater : public QObject {
	Q_OBJECT

private slots:
	void versions()
	{
		QVERIFY(isNewer(QStringLiteral("1.1.0"), QStringLiteral("1.0.0")));
		QVERIFY(isNewer(QStringLiteral("1.10.0"), QStringLiteral("1.9.2")));
		QVERIFY(isNewer(QStringLiteral("v2.0"), QStringLiteral("1.99.99")));
		QVERIFY(!isNewer(QStringLiteral("1.0.0"), QStringLiteral("1.0.0")));
		QVERIFY(!isNewer(QStringLiteral("1.0.9"), QStringLiteral("1.1.0")));
		QVERIFY(!isNewer(QString(), QStringLiteral("1.0.0")));
		QCOMPARE(compareVersions(QStringLiteral("1.2.0-beta1"), QStringLiteral("1.2.0")), 0);
	}

	void releaseFromGitHub()
	{
		const QString hash(64, QLatin1Char('a'));
		const QString body = QStringLiteral("## Novidades\n- feat: x\n\n### Checksums\n"
						    "    meketreve-obs-essentials-1.1.0-windows-x64-installer.exe: ") +
				     hash +
				     QStringLiteral("\n    meketreve-obs-essentials-1.1.0-x86_64-linux-gnu.deb: ") +
				     hash.toUpper() + QStringLiteral("\n");
		QJsonArray assets;
		for (const char *name : {"meketreve-obs-essentials-1.1.0-windows-x64-installer.exe",
					 "meketreve-obs-essentials-1.1.0-windows-x64.zip",
					 "meketreve-obs-essentials-1.1.0-x86_64-linux-gnu-dbgsym.ddeb",
					 "meketreve-obs-essentials-1.1.0-x86_64-linux-gnu.deb",
					 "meketreve-obs-essentials-1.1.0-macos-universal.pkg"})
			assets.append(QJsonObject{{QStringLiteral("name"), QLatin1String(name)},
						  {QStringLiteral("browser_download_url"),
						   QStringLiteral("https://example.invalid/") + QLatin1String(name)}});
		const Release r =
			parseRelease(QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("1.1.0")},
						 {QStringLiteral("body"), body},
						 {QStringLiteral("html_url"), QStringLiteral("https://x/1.1.0")},
						 {QStringLiteral("assets"), assets}});
		QCOMPARE(r.version, QStringLiteral("1.1.0"));
		QCOMPARE(r.notes, QStringLiteral("## Novidades\n- feat: x"));
		QCOMPARE(r.checksums.size(), 2);
		QCOMPARE(r.checksums.value(QStringLiteral("meketreve-obs-essentials-1.1.0-x86_64-linux-gnu.deb")),
			 hash);
		QCOMPARE(pickAsset(r, Package::WindowsInstaller),
			 QStringLiteral("meketreve-obs-essentials-1.1.0-windows-x64-installer.exe"));
		QCOMPARE(pickAsset(r, Package::Deb),
			 QStringLiteral("meketreve-obs-essentials-1.1.0-x86_64-linux-gnu.deb"));
		QCOMPARE(pickAsset(r, Package::MacPkg),
			 QStringLiteral("meketreve-obs-essentials-1.1.0-macos-universal.pkg"));
	}

	void notesInThePluginLanguage()
	{
		const QString both =
			QStringLiteral("<!-- lang:pt -->\n## Novidades\n- **chat:** emotes animados (abc1234)\n\n"
				       "<!-- lang:en -->\n## New\n- **chat:** animated emotes (abc1234)\n");
		QCOMPARE(notesFor(both, QStringLiteral("pt")),
			 QStringLiteral("## Novidades\n- **chat:** emotes animados (abc1234)"));
		QCOMPARE(notesFor(both, QStringLiteral("en")),
			 QStringLiteral("## New\n- **chat:** animated emotes (abc1234)"));
		/* Unknown language: English. Old notes without markers: whole. */
		QCOMPARE(notesFor(both, QStringLiteral("es")),
			 QStringLiteral("## New\n- **chat:** animated emotes (abc1234)"));
		const QString old = QStringLiteral("## ✨ Novidades / New\n- **x:** y (1234567)");
		QCOMPARE(notesFor(old, QStringLiteral("pt")), old);
	}

	void noChecksumsNoAssets()
	{
		const Release r = parseRelease(QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("v1.2.0")},
							   {QStringLiteral("body"), QStringLiteral("só notas")}});
		QCOMPARE(r.version, QStringLiteral("1.2.0"));
		QVERIFY(r.checksums.isEmpty());
		QCOMPARE(r.notes, QStringLiteral("só notas"));
		QVERIFY(pickAsset(r, Package::Deb).isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestUpdater)
#include "test-updater.moc"
