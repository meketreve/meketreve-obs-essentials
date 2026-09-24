/*
Meketreve OBS Essentials - Outputs
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

#include <QJsonObject>
#include <QUuid>

const QList<PlatformDefaults> &outputPlatforms()
{
	/* Ingest servers every account of that platform can use. Kick and
	 * TikTok hand out per-account URLs too; the user can paste those. */
	static const QList<PlatformDefaults> platforms{
		{"twitch", "Twitch", "rtmp://live.twitch.tv/app", 6000},
		{"youtube", "YouTube", "rtmps://a.rtmps.youtube.com:443/live2", 51000},
		{"kick", "Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net:443/app", 8000},
		{"tiktok", "TikTok", "", 6000},
		{"custom", "RTMP(S)", "", 0},
		{"srt", "SRT", "srt://", 0},
	};
	return platforms;
}

const PlatformDefaults *findPlatform(const QString &id)
{
	for (const PlatformDefaults &p : outputPlatforms()) {
		if (id == QLatin1String(p.id))
			return &p;
	}
	return nullptr;
}

bool OutputConfig::isSrt() const
{
	return server.startsWith(QLatin1String("srt://"), Qt::CaseInsensitive) ||
	       server.startsWith(QLatin1String("rist://"), Qt::CaseInsensitive);
}

QString OutputConfig::outputType() const
{
	return isSrt() ? QStringLiteral("ffmpeg_mpegts_muxer") : QStringLiteral("rtmp_output");
}

QJsonObject OutputConfig::toJson(bool withKey) const
{
	QJsonObject o{{QStringLiteral("id"), id},
		      {QStringLiteral("name"), name},
		      {QStringLiteral("platform"), platform},
		      {QStringLiteral("server"), server},
		      {QStringLiteral("enabled"), enabled},
		      {QStringLiteral("followMain"), followMain},
		      {QStringLiteral("sharedEncoder"), sharedEncoder},
		      {QStringLiteral("videoEncoder"), videoEncoder},
		      {QStringLiteral("videoBitrate"), videoBitrate},
		      {QStringLiteral("audioBitrate"), audioBitrate}};
	if (!canvas.isEmpty())
		o.insert(QStringLiteral("canvas"), canvas);
	if (withKey && !key.isEmpty())
		o.insert(QStringLiteral("key"), key);
	return o;
}

OutputConfig OutputConfig::fromJson(const QJsonObject &o)
{
	OutputConfig c;
	c.id = o.value(QStringLiteral("id")).toString();
	if (c.id.isEmpty())
		c.id = newOutputId();
	c.platform = o.value(QStringLiteral("platform")).toString(QStringLiteral("custom"));
	if (!findPlatform(c.platform))
		c.platform = QStringLiteral("custom");
	c.name = o.value(QStringLiteral("name")).toString();
	if (c.name.isEmpty())
		c.name = QString::fromLatin1(findPlatform(c.platform)->label);
	c.server = o.value(QStringLiteral("server")).toString().trimmed();
	c.key = o.value(QStringLiteral("key")).toString();
	c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
	c.followMain = o.value(QStringLiteral("followMain")).toBool(true);
	c.sharedEncoder = o.value(QStringLiteral("sharedEncoder")).toBool(true);
	c.videoEncoder = o.value(QStringLiteral("videoEncoder")).toString(QStringLiteral("obs_x264"));
	c.videoBitrate = qBound(200, o.value(QStringLiteral("videoBitrate")).toInt(6000), 100000);
	c.audioBitrate = qBound(32, o.value(QStringLiteral("audioBitrate")).toInt(160), 512);
	c.canvas = o.value(QStringLiteral("canvas")).toString();
	return c;
}

QJsonArray outputsToJson(const QList<OutputConfig> &outputs, bool withKeys)
{
	QJsonArray arr;
	for (const OutputConfig &c : outputs)
		arr.append(c.toJson(withKeys));
	return arr;
}

QList<OutputConfig> outputsFromJson(const QJsonArray &arr)
{
	QList<OutputConfig> out;
	for (const QJsonValue v : arr) {
		if (v.isObject())
			out.append(OutputConfig::fromJson(v.toObject()));
	}
	return out;
}

QString newOutputId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}
