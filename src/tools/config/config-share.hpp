/*
Meketreve OBS Essentials - Shared configuration
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

#include <QJsonValue>
#include <QString>

#include <functional>

/* One part of the shareable configuration ("tabs", "chat", "outputs"). The
 * tool that owns the data registers how to save, load and describe it. */
struct ConfigSection {
	QString key;
	const char *labelKey;
	std::function<QJsonValue()> save;
	std::function<void(const QJsonValue &)> load;
	/* One line for the dialogs, e.g. "Twitch: xqc, Kick: westcol". */
	std::function<QString(const QJsonValue &)> describe;
};

void configShareAddSection(const ConfigSection &section);
void configShareOpenExport();
void configShareOpenImport();
