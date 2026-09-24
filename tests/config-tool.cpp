/*
Meketreve OBS Essentials - config-tool (developer tool)
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

/* config-tool encode <bundle.json>   -> prints the MOE1: string
 * config-tool decode <MOE1:...>      -> prints the JSON */

#include "config-codec.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>

#include <cstdio>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 3) {
		std::fprintf(stderr, "usage: config-tool encode <file.json> | decode <MOE1:...>\n");
		return 2;
	}
	if (args[1] == QLatin1String("encode")) {
		QFile file(args[2]);
		if (!file.open(QIODevice::ReadOnly))
			return 1;
		const QJsonObject bundle = QJsonDocument::fromJson(file.readAll()).object();
		QString error;
		if (!ConfigCodec::validate(bundle, &error)) {
			std::fprintf(stderr, "%s\n", qPrintable(error));
			return 1;
		}
		std::printf("%s\n", qPrintable(ConfigCodec::encode(bundle)));
		return 0;
	}
	QJsonObject bundle;
	QString error;
	if (!ConfigCodec::decode(args[2], bundle, &error)) {
		std::fprintf(stderr, "%s\n", qPrintable(error));
		return 1;
	}
	std::printf("%s", QJsonDocument(bundle).toJson().constData());
	return 0;
}
