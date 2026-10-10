/*
Meketreve OBS Essentials - PNGTuber tests
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
#include "pngtuber-logic.hpp"

#include <QTest>

#include <cmath>
#include <vector>

using PngTuber::Avatar;
using PngTuber::Settings;

class TestPngTuber : public QObject {
	Q_OBJECT

private slots:
	void talksAboveTheThresholdAndHolds()
	{
		Avatar a([]() { return 0.5; });
		Settings s;
		s.thresholdDb = -40.0;
		s.holdMs = 200.0;
		s.blink = false;
		s.hopPx = 0.0;
		a.setSettings(s);
		a.setLevel(-60.0);
		a.tick(0.1);
		QVERIFY(!a.talking());
		a.setLevel(-30.0);
		a.tick(0.016);
		QVERIFY(a.talking());
		/* A short pause between words keeps the mouth open. */
		a.setLevel(-70.0);
		a.tick(0.15);
		QVERIFY(a.talking());
		a.setLevel(-30.0);
		a.tick(0.016);
		a.setLevel(-70.0);
		a.tick(0.15);
		QVERIFY(a.talking());
		a.tick(0.06);
		QVERIFY(!a.talking());
	}

	void blinksOnSchedule()
	{
		/* rand 0 = the shortest wait, every time. */
		Avatar a([]() { return 0.0; });
		Settings s;
		s.blinkMinS = 2.0;
		s.blinkMaxS = 6.0;
		s.blinkMs = 100.0;
		a.setSettings(s);
		a.tick(1.9);
		QVERIFY(!a.eyesClosed());
		a.tick(0.2);
		QVERIFY(a.eyesClosed());
		a.tick(0.05);
		QVERIFY(a.eyesClosed());
		a.tick(0.06);
		QVERIFY(!a.eyesClosed());
		/* The next one comes after another 2 s. */
		a.tick(1.9);
		QVERIFY(!a.eyesClosed());
		a.tick(0.2);
		QVERIFY(a.eyesClosed());
		/* Off: never. */
		Avatar b([]() { return 0.0; });
		s.blink = false;
		b.setSettings(s);
		b.tick(10.0);
		QVERIFY(!b.eyesClosed());
	}

	void hopsWhenItStartsTalking()
	{
		Avatar a([]() { return 0.5; });
		Settings s;
		s.blink = false;
		s.hopPx = 10.0;
		s.hopMs = 200.0;
		s.bobPx = 0.0;
		a.setSettings(s);
		QCOMPARE(a.maxLift(), 10.0);
		QCOMPARE(a.lift(), 0.0);
		a.setLevel(0.0);
		a.tick(0.1); /* half way up and down: the top */
		QVERIFY(std::abs(a.lift() - 10.0) < 1e-9);
		a.tick(0.11);
		QCOMPARE(a.lift(), 0.0);
		/* Still talking: no new hop until it stops and starts again. */
		a.tick(0.1);
		QCOMPARE(a.lift(), 0.0);
		/* Bobbing while talking stays within its height. */
		s.bobPx = 4.0;
		a.setSettings(s);
		for (int i = 0; i < 50; ++i) {
			a.tick(0.013);
			QVERIFY(a.lift() >= 0.0 && a.lift() <= 4.0 + 1e-9);
		}
	}

	void levelInDecibels()
	{
		const std::vector<float> full(480, 1.0f);
		QVERIFY(std::abs(Avatar::levelDb(full.data(), 480)) < 1e-9);
		const std::vector<float> tenth(480, 0.1f);
		QVERIFY(std::abs(Avatar::levelDb(tenth.data(), 480) + 20.0) < 1e-6);
		const std::vector<float> silence(480, 0.0f);
		QCOMPARE(Avatar::levelDb(silence.data(), 480), -120.0);
		QCOMPARE(Avatar::levelDb(nullptr, 0), -120.0);
	}
};

QTEST_GUILESS_MAIN(TestPngTuber)
#include "test-pngtuber.moc"
