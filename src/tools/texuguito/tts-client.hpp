/*
Meketreve OBS Essentials - Texuguito bot
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

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;

/* Google Translate's text-to-speech in Brazilian Portuguese, the way gTTS
 * does it (same endpoint the original bot used): the text goes in chunks of
 * up to 100 characters and the MP3 pieces are joined. Needs internet. */
namespace GoogleTts {

QStringList splitText(const QString &text, int maxChars = 100);
QByteArray requestBody(const QString &chunk, const QString &lang = QStringLiteral("pt"));
/* The MP3 inside a batchexecute answer, or empty. */
QByteArray parseResponse(const QByteArray &body);
void synthesize(QNetworkAccessManager *net, const QString &text,
		std::function<void(QByteArray mp3, QString error)> done, QObject *context,
		const QString &lang = QStringLiteral("pt"));

} // namespace GoogleTts
