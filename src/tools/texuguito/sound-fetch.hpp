/*
Meketreve OBS Essentials
Copyright (C) 2026 Meketreve

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
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

/* Downloads a sound effect for the soundboard from a link: a direct audio
 * file (.mp3, .wav, .ogg) or a page that links one, such as a myinstants
 * page. https only, and small files only. */
namespace SoundFetch {

/* Pages and sounds alike: a sound effect is far smaller than this. */
constexpr qint64 kMaxAudioBytes = 3 * 1024 * 1024;

enum class Error { None, BadLink, Network, Blocked, NoAudio, TooBig };

struct Result {
	Error error = Error::None;
	QByteArray data;
	QString ext;  /* "mp3", "wav" or "ogg" */
	QString name; /* suggested clip name, from the file name */
	QString detail;
};

/* Only https links to a public host (no localhost or private addresses). */
bool allowedUrl(const QUrl &url);
/* "mp3", "wav" or "ogg" from the first bytes, or empty. */
QString audioType(const QByteArray &head);
/* The sound a page links (og:audio, then the first .mp3/.ogg/.wav link). */
QUrl audioUrlInPage(const QByteArray &html, const QUrl &page);
/* A clip name: lowercase ASCII letters, digits, "-" and "_", up to 32. */
QString clipName(const QString &raw);
QString nameFromUrl(const QUrl &url);

void fetch(QNetworkAccessManager *net, const QString &link, std::function<void(const Result &)> done, QObject *context);

} // namespace SoundFetch
