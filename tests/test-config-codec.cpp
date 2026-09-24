/*
Meketreve OBS Essentials - configuration string unit tests (developer tool)
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

#include "config-codec.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

class TestConfigCodec : public QObject {
	Q_OBJECT

private slots:
	void roundTrip()
	{
		const QJsonObject in{{QStringLiteral("name"), QStringLiteral("Minha config ção")},
				     {QStringLiteral("chat"),
				      QJsonObject{{QStringLiteral("twitch"), QStringLiteral("xqc")}}},
				     {QStringLiteral("outputs"), QJsonArray{QJsonObject{{QStringLiteral("name"), 1}}}}};
		const QString text = ConfigCodec::encode(in);
		QVERIFY(text.startsWith(QLatin1String("MOE1:")));
		/* URL-safe alphabet: no characters that chat apps mangle. */
		QVERIFY(!text.contains(QLatin1Char('+')));
		QVERIFY(!text.contains(QLatin1Char('/')));
		QVERIFY(!text.contains(QLatin1Char('=')));

		QJsonObject out;
		QString error;
		QVERIFY2(ConfigCodec::decode(text, out, &error), qPrintable(error));
		QCOMPARE(out.value(QStringLiteral("format")).toInt(), 1);
		QCOMPARE(out.value(QStringLiteral("name")).toString(), QStringLiteral("Minha config ção"));
		QCOMPARE(out.value(QStringLiteral("chat")).toObject().value(QStringLiteral("twitch")).toString(),
			 QStringLiteral("xqc"));
	}

	void survivesLineWrapping()
	{
		const QString text = ConfigCodec::encode(QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
		QString wrapped;
		for (qsizetype i = 0; i < text.size(); i += 10)
			wrapped += text.mid(i, 10) + QStringLiteral("\r\n  ");
		QJsonObject out;
		QVERIFY(ConfigCodec::decode(wrapped, out));
	}

	void rejectsGarbage()
	{
		QJsonObject out;
		QString error;
		QVERIFY(!ConfigCodec::decode(QStringLiteral("hello"), out, &error));
		QVERIFY(!ConfigCodec::decode(QStringLiteral("MOE1:"), out, &error));
		QVERIFY(!ConfigCodec::decode(QStringLiteral("MOE1:!!!!"), out, &error));
		QVERIFY(!ConfigCodec::decode(QStringLiteral("MOE1:AAAAAAAA"), out, &error));

		QString text = ConfigCodec::encode(QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
		text.chop(6);
		QVERIFY(!ConfigCodec::decode(text, out, &error));
	}

	void rejectsFutureVersion()
	{
		const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("format"), 2}}).toJson();
		const QString text = QStringLiteral("MOE1:") +
				     QString::fromLatin1(qCompress(json).toBase64(QByteArray::Base64UrlEncoding));
		QJsonObject out;
		QString error;
		QVERIFY(!ConfigCodec::decode(text, out, &error));
		QVERIFY(error.contains(QStringLiteral("newer")));
	}

	void rejectsZipBomb()
	{
		/* A header that claims 100 MB is refused before inflating. */
		QByteArray packed = qCompress(QByteArray(1000, 'a'));
		packed[0] = 0x06;
		const QString text =
			QStringLiteral("MOE1:") + QString::fromLatin1(packed.toBase64(QByteArray::Base64UrlEncoding));
		QJsonObject out;
		QString error;
		QVERIFY(!ConfigCodec::decode(text, out, &error));
		QVERIFY(error.contains(QStringLiteral("large")));
	}

	void bundledPresetsAreValid()
	{
		const QDir dir(QStringLiteral(PRESETS_DIR));
		const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
		QVERIFY(!files.isEmpty());
		for (const QString &f : files) {
			QFile file(dir.filePath(f));
			QVERIFY(file.open(QIODevice::ReadOnly));
			QJsonParseError parseError{};
			const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
			QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(f));
			QString error;
			QVERIFY2(ConfigCodec::validate(doc.object(), &error),
				 qPrintable(f + QStringLiteral(": ") + error));
			const QString text = ConfigCodec::encode(doc.object());
			QJsonObject back;
			QVERIFY(ConfigCodec::decode(text, back));
			QCOMPARE(back, doc.object());
			/* Presets never carry stream keys. */
			for (const QJsonValue o : doc.object().value(QStringLiteral("outputs")).toArray())
				QVERIFY(!o.toObject().contains(QStringLiteral("key")));
		}
	}
};

QTEST_GUILESS_MAIN(TestConfigCodec)
#include "test-config-codec.moc"
