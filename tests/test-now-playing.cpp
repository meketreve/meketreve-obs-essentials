/*
Meketreve OBS Essentials - Now Playing unit tests (developer tool)
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

#include "media-info.hpp"
#include "spectrum.hpp"

#include <QTest>

#include <cmath>
#include <vector>

namespace {

std::vector<float> sine(double hz, int rate, int count, float amp)
{
	std::vector<float> out(static_cast<size_t>(count));
	for (int i = 0; i < count; i++)
		out[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2 * 3.14159265358979 * hz * i / rate));
	return out;
}

int loudest(const Spectrum::Frame &frame)
{
	int best = 0;
	for (int i = 1; i < frame.bands.size(); i++)
		if (frame.bands[i] > frame.bands[best])
			best = i;
	return best;
}

} // namespace

class TestNowPlaying : public QObject {
	Q_OBJECT

private slots:
	void silenceIsQuiet()
	{
		Spectrum spectrum(48000);
		const Spectrum::Frame frame = spectrum.analyse();
		QCOMPARE(frame.bands.size(), Spectrum::kBands);
		QVERIFY(frame.rmsDb < -100);
		for (float db : frame.bands)
			QVERIFY(db < -100);
	}

	void sinePeaksInItsBand_data()
	{
		QTest::addColumn<int>("rate");
		QTest::addColumn<double>("hz");
		QTest::newRow("48k 1 kHz") << 48000 << 1000.0;
		QTest::newRow("48k 100 Hz") << 48000 << 100.0;
		QTest::newRow("44.1k 5 kHz") << 44100 << 5000.0;
		QTest::newRow("24k 440 Hz") << 24000 << 440.0;
	}
	void sinePeaksInItsBand()
	{
		QFETCH(int, rate);
		QFETCH(double, hz);
		Spectrum spectrum(rate);
		const std::vector<float> wave = sine(hz, rate, spectrum.size() * 2, 0.5f);
		spectrum.push(wave.data(), wave.size());
		const Spectrum::Frame frame = spectrum.analyse();
		const double center = spectrum.bandCenter(loudest(frame));
		/* Bands are ~1/7 octave wide: the peak lands within one of them. */
		QVERIFY2(std::abs(std::log2(center / hz)) < 0.15,
			 qPrintable(QStringLiteral("peak at %1 Hz").arg(center)));
		/* RMS of a 0.5 sine = -9 dB. */
		QVERIFY(std::abs(frame.rmsDb - (-9.03f)) < 0.5f);
	}

	void clearForgetsTheAudio()
	{
		Spectrum spectrum(48000);
		const std::vector<float> wave = sine(1000, 48000, spectrum.size(), 0.5f);
		spectrum.push(wave.data(), wave.size());
		spectrum.clear();
		QVERIFY(spectrum.analyse().rmsDb < -100);
	}

	void mediaJson()
	{
		MediaInfo none;
		QVERIFY(none.toJson().isEmpty());
		MediaInfo song;
		song.player = QStringLiteral("spotify");
		song.title = QStringLiteral("Song");
		song.playing = true;
		song.length = 200;
		const QJsonObject json = song.toJson();
		QCOMPARE(json.value(QStringLiteral("title")).toString(), QStringLiteral("Song"));
		QCOMPARE(json.value(QStringLiteral("playing")).toBool(), true);
		QCOMPARE(json.value(QStringLiteral("length")).toDouble(), 200.0);
	}
};

QTEST_GUILESS_MAIN(TestNowPlaying)
#include "test-now-playing.moc"
