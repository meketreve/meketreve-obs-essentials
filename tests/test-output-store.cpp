/*
Meketreve OBS Essentials - output store unit tests (developer tool)
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

#include "output-store.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestOutputStore : public QObject {
	Q_OBJECT

private slots:
	void keyNeverExportedUnlessAsked()
	{
		OutputConfig c;
		c.id = QStringLiteral("a");
		c.name = QStringLiteral("Twitch");
		c.platform = QStringLiteral("twitch");
		c.server = QStringLiteral("rtmp://live.twitch.tv/app");
		c.key = QStringLiteral("live_123_secret");

		QVERIFY(!c.toJson(false).contains(QStringLiteral("key")));
		QCOMPARE(c.toJson(true).value(QStringLiteral("key")).toString(), c.key);
		const QByteArray exported = QJsonDocument(outputsToJson({c}, false)).toJson();
		QVERIFY(!exported.contains("live_123_secret"));
	}

	void roundTrip()
	{
		OutputConfig c;
		c.id = QStringLiteral("x1");
		c.name = QStringLiteral("SRT box");
		c.platform = QStringLiteral("srt");
		c.server = QStringLiteral("srt://10.0.0.2:9000?mode=caller");
		c.sharedEncoder = false;
		c.followMain = false;
		c.videoEncoder = QStringLiteral("obs_nvenc_h264_tex");
		c.videoBitrate = 12000;
		c.audioBitrate = 192;
		c.canvas = QStringLiteral("Vertical");

		const QList<OutputConfig> back = outputsFromJson(outputsToJson({c}, true));
		QCOMPARE(back.size(), 1);
		const OutputConfig &b = back[0];
		QCOMPARE(b.id, c.id);
		QCOMPARE(b.server, c.server);
		QCOMPARE(b.sharedEncoder, false);
		QCOMPARE(b.followMain, false);
		QCOMPARE(b.videoEncoder, c.videoEncoder);
		QCOMPARE(b.videoBitrate, 12000);
		QCOMPARE(b.audioBitrate, 192);
		QCOMPARE(b.canvas, QStringLiteral("Vertical"));
		QVERIFY(b.isSrt());
		QCOMPARE(b.outputType(), QStringLiteral("ffmpeg_mpegts_muxer"));
	}

	void sanitizesInput()
	{
		const QJsonObject o{{QStringLiteral("platform"), QStringLiteral("myspace")},
				    {QStringLiteral("videoBitrate"), 999999},
				    {QStringLiteral("audioBitrate"), 1},
				    {QStringLiteral("server"), QStringLiteral("  rtmp://x/app  ")}};
		const OutputConfig c = OutputConfig::fromJson(o);
		QCOMPARE(c.platform, QStringLiteral("custom"));
		QVERIFY(!c.id.isEmpty());
		QCOMPARE(c.name, QStringLiteral("RTMP(S)"));
		QCOMPARE(c.videoBitrate, 100000);
		QCOMPARE(c.audioBitrate, 32);
		QCOMPARE(c.server, QStringLiteral("rtmp://x/app"));
		QCOMPARE(c.outputType(), QStringLiteral("rtmp_output"));
		QVERIFY(c.sharedEncoder);
		QVERIFY(c.followMain);
	}

	void platformsHaveDefaults()
	{
		QVERIFY(findPlatform(QStringLiteral("twitch")));
		QVERIFY(findPlatform(QStringLiteral("youtube")));
		QVERIFY(findPlatform(QStringLiteral("kick")));
		QVERIFY(!findPlatform(QStringLiteral("nope")));
		QVERIFY(QString::fromLatin1(findPlatform(QStringLiteral("youtube"))->server).startsWith("rtmps://"));
	}
};

QTEST_GUILESS_MAIN(TestOutputStore)
#include "test-output-store.moc"
