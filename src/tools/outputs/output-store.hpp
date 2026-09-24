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
#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>

struct PlatformDefaults {
	const char *id;
	const char *label;
	const char *server;
	/* Highest video bitrate (kbps) the platform takes for a regular
	 * account; 0 = no practical limit. Used only for a warning. */
	int maxVideoKbps;
};

const QList<PlatformDefaults> &outputPlatforms();
const PlatformDefaults *findPlatform(const QString &id);

struct OutputConfig {
	QString id;
	QString name;
	QString platform = QStringLiteral("custom");
	QString server;
	QString key; /* local only: never exported */
	bool enabled = true;
	/* Start and stop together with the main stream. */
	bool followMain = true;
	/* true: reuse the main stream's encoders (no extra CPU/GPU cost).
	 * false: this output encodes on its own with the settings below. */
	bool sharedEncoder = true;
	QString videoEncoder = QStringLiteral("obs_x264");
	int videoBitrate = 6000;
	int audioBitrate = 160;
	/* Canvas name for the output's own encoder; empty = main canvas. */
	QString canvas;

	bool isSrt() const;
	/* Output type for this destination ("rtmp_output" or "ffmpeg_mpegts_muxer"). */
	QString outputType() const;
	QJsonObject toJson(bool withKey) const;
	static OutputConfig fromJson(const QJsonObject &o);
};

QJsonArray outputsToJson(const QList<OutputConfig> &outputs, bool withKeys);
QList<OutputConfig> outputsFromJson(const QJsonArray &arr);
QString newOutputId();
