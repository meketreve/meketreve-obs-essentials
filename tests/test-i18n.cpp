/*
Meketreve OBS Essentials - Languages
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

#include "i18n.hpp"

#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTest>

using namespace I18n;

namespace {

QHash<QString, QString> readLocale(const char *name)
{
	QFile file(QStringLiteral(MEKETREVE_LOCALE_DIR "/") + QLatin1String(name));
	return file.open(QIODevice::ReadOnly) ? parseIni(file.readAll()) : QHash<QString, QString>();
}

} // namespace

class TestI18n : public QObject {
	Q_OBJECT

private slots:
	void settings()
	{
		const Settings empty = parseSettings(QJsonObject());
		QCOMPARE(empty.ui, QStringLiteral("auto"));
		QCOMPARE(empty.stream, QStringLiteral("plugin"));

		const Settings junk = parseSettings({{QStringLiteral("ui"), QStringLiteral("fr")},
						     {QStringLiteral("stream"), QStringLiteral("es")}});
		QCOMPARE(junk.ui, QStringLiteral("auto"));
		QCOMPARE(junk.stream, QStringLiteral("plugin"));

		Settings s;
		s.ui = QStringLiteral("pt");
		s.stream = QStringLiteral("both");
		const Settings back = parseSettings(toJson(s));
		QCOMPARE(back.ui, s.ui);
		QCOMPARE(back.stream, s.stream);
	}

	void pluginFollowsObsOrChoice()
	{
		QCOMPARE(pluginLanguage(QStringLiteral("auto"), QStringLiteral("pt-BR")), QStringLiteral("pt"));
		QCOMPARE(pluginLanguage(QStringLiteral("auto"), QStringLiteral("pt-PT")), QStringLiteral("pt"));
		QCOMPARE(pluginLanguage(QStringLiteral("auto"), QStringLiteral("en-US")), QStringLiteral("en"));
		QCOMPARE(pluginLanguage(QStringLiteral("auto"), QStringLiteral("es-ES")), QStringLiteral("en"));
		QCOMPARE(pluginLanguage(QStringLiteral("pt"), QStringLiteral("en-US")), QStringLiteral("pt"));
		QCOMPARE(pluginLanguage(QStringLiteral("en"), QStringLiteral("pt-BR")), QStringLiteral("en"));
	}

	void streamFollowsPluginOrChoice()
	{
		QCOMPARE(streamLanguage(QStringLiteral("plugin"), QStringLiteral("pt")), QStringLiteral("pt"));
		QCOMPARE(streamLanguage(QStringLiteral("plugin"), QStringLiteral("en")), QStringLiteral("en"));
		QCOMPARE(streamLanguage(QStringLiteral("en"), QStringLiteral("pt")), QStringLiteral("en"));
		QCOMPARE(streamLanguage(QStringLiteral("both"), QStringLiteral("pt")), QStringLiteral("both"));
		QCOMPARE(localeOf(QStringLiteral("pt")), QStringLiteral("pt-BR"));
		QCOMPARE(localeOf(QStringLiteral("both")), QStringLiteral("en-US"));
	}

	void ini()
	{
		const auto t = parseIni("# comment\nA=\"one\"\nB = \"say \\\"hi\\\"\"\nC=bare\n\nbroken line\n");
		QCOMPARE(t.value(QStringLiteral("A")), QStringLiteral("one"));
		QCOMPARE(t.value(QStringLiteral("B")), QStringLiteral("say \"hi\""));
		QCOMPARE(t.value(QStringLiteral("C")), QStringLiteral("bare"));
		QCOMPARE(t.size(), 3);
	}

	void catalogFallback()
	{
		Catalog c;
		c.set(QStringLiteral("en"),
		      {{QStringLiteral("Hi"), QStringLiteral("Hello")}, {QStringLiteral("Ok"), QStringLiteral("OK")}});
		c.set(QStringLiteral("pt"), {{QStringLiteral("Hi"), QStringLiteral("Olá")}});
		QCOMPARE(c.text(QStringLiteral("pt"), QStringLiteral("Hi")), QStringLiteral("Olá"));
		QCOMPARE(c.text(QStringLiteral("pt"), QStringLiteral("Ok")), QStringLiteral("OK"));
		QCOMPARE(c.text(QStringLiteral("en"), QStringLiteral("Hi")), QStringLiteral("Hello"));
		QCOMPARE(c.text(QStringLiteral("pt"), QStringLiteral("Nope")), QStringLiteral("Nope"));
		QCOMPARE(c.both(QStringLiteral("Hi")), QStringLiteral("Olá / Hello"));
		QCOMPARE(c.both(QStringLiteral("Ok")), QStringLiteral("OK"));
	}

	void composeForTheStream()
	{
		Catalog c;
		c.set(QStringLiteral("en"), {{QStringLiteral("Follow"), QStringLiteral("%1 followed")},
					     {QStringLiteral("Pts"), QStringLiteral("%1 pts")}});
		c.set(QStringLiteral("pt"), {{QStringLiteral("Follow"), QStringLiteral("%1 seguiu")},
					     {QStringLiteral("Pts"), QStringLiteral("%1 pts")}});
		const auto follow = [](const Text &t) {
			return t("Follow").arg(QStringLiteral("ana"));
		};
		QCOMPARE(compose(QStringLiteral("pt"), c, follow), QStringLiteral("ana seguiu"));
		QCOMPARE(compose(QStringLiteral("en"), c, follow), QStringLiteral("ana followed"));
		QCOMPARE(compose(QStringLiteral("both"), c, follow), QStringLiteral("ana seguiu / ana followed"));
		QCOMPARE(compose(QStringLiteral("both"), c, [](const Text &t) { return t("Pts").arg(5); }),
			 QStringLiteral("5 pts"));
		/* A joined template still takes .arg() in both halves. */
		QCOMPARE(c.both(QStringLiteral("Follow")).arg(QStringLiteral("bo")),
			 QStringLiteral("bo seguiu / bo followed"));
	}

	void swapOnlyUntouchedDefaults()
	{
		const auto defaults = [](const QString &follow, const QString &goal) {
			return QJsonObject{
				{QStringLiteral("types"),
				 QJsonObject{{QStringLiteral("follow"),
					      QJsonObject{{QStringLiteral("text"), follow},
							  {QStringLiteral("image"), QStringLiteral("star.svg")}}},
					     {QStringLiteral("sub"), QJsonObject{{QStringLiteral("text"), follow}}}}},
				{QStringLiteral("goals"), QJsonArray{QJsonObject{{QStringLiteral("title"), goal}}}}};
		};
		const QJsonObject pt = defaults(QStringLiteral("{name} seguiu!"), QStringLiteral("Meta de follows"));
		const QJsonObject en = defaults(QStringLiteral("{name} followed!"), QStringLiteral("Follower goal"));

		QJsonObject saved = pt;
		QJsonObject types = saved.value(QStringLiteral("types")).toObject();
		QJsonObject sub = types.value(QStringLiteral("sub")).toObject();
		sub.insert(QStringLiteral("text"), QStringLiteral("valeu {name}"));
		types.insert(QStringLiteral("sub"), sub);
		saved.insert(QStringLiteral("types"), types);
		QJsonArray goals = saved.value(QStringLiteral("goals")).toArray();
		goals.append(QJsonObject{{QStringLiteral("title"), QStringLiteral("Meta de follows")}});
		saved.insert(QStringLiteral("goals"), goals);
		saved.insert(QStringLiteral("extra"), QStringLiteral("Meta de follows"));

		const QJsonObject out = swapDefaults(saved, {pt, en}, en).toObject();
		const QJsonObject outTypes = out.value(QStringLiteral("types")).toObject();
		QCOMPARE(outTypes.value(QStringLiteral("follow")).toObject().value(QStringLiteral("text")).toString(),
			 QStringLiteral("{name} followed!"));
		QCOMPARE(outTypes.value(QStringLiteral("follow")).toObject().value(QStringLiteral("image")).toString(),
			 QStringLiteral("star.svg"));
		QCOMPARE(outTypes.value(QStringLiteral("sub")).toObject().value(QStringLiteral("text")).toString(),
			 QStringLiteral("valeu {name}"));
		const QJsonArray outGoals = out.value(QStringLiteral("goals")).toArray();
		QCOMPARE(outGoals.at(0).toObject().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Follower goal"));
		/* Only the spots the defaults have: a second goal or an extra field stays. */
		QCOMPARE(outGoals.at(1).toObject().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Meta de follows"));
		QCOMPARE(out.value(QStringLiteral("extra")).toString(), QStringLiteral("Meta de follows"));
		/* Back to Portuguese. */
		const QJsonObject back = swapDefaults(out, {pt, en}, pt).toObject();
		QCOMPARE(back.value(QStringLiteral("types"))
				 .toObject()
				 .value(QStringLiteral("follow"))
				 .toObject()
				 .value(QStringLiteral("text"))
				 .toString(),
			 QStringLiteral("{name} seguiu!"));
	}

	void localeFilesHaveTheSameKeys()
	{
		const auto en = readLocale("en-US.ini");
		const auto pt = readLocale("pt-BR.ini");
		QVERIFY(en.size() > 100);
		const QStringList enKeys = en.keys();
		const QStringList ptKeys = pt.keys();
		const QSet<QString> enSet(enKeys.begin(), enKeys.end());
		const QSet<QString> ptSet(ptKeys.begin(), ptKeys.end());
		QStringList onlyEn = QSet<QString>(enSet).subtract(ptSet).values();
		QStringList onlyPt = QSet<QString>(ptSet).subtract(enSet).values();
		onlyEn.sort();
		onlyPt.sort();
		QVERIFY2(onlyEn.isEmpty(), qPrintable(QStringLiteral("only en-US: ") + onlyEn.join(QLatin1Char(' '))));
		QVERIFY2(onlyPt.isEmpty(), qPrintable(QStringLiteral("only pt-BR: ") + onlyPt.join(QLatin1Char(' '))));
	}
};

QTEST_GUILESS_MAIN(TestI18n)
#include "test-i18n.moc"
