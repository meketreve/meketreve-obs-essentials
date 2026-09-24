/*
Meketreve OBS Essentials - Unified Chat
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
#include "oauth-util.hpp"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>

namespace OAuthUtil {

namespace {

QByteArray randomUrlSafe(int bytes)
{
	QByteArray raw(bytes, Qt::Uninitialized);
	for (int i = 0; i < bytes; i++)
		raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
	return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace

QByteArray newCodeVerifier()
{
	return randomUrlSafe(48); /* 64 characters */
}

QByteArray codeChallengeS256(const QByteArray &verifier)
{
	return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
		.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray newState()
{
	return randomUrlSafe(18);
}

QString parseRequestLine(const QByteArray &requestLine, QUrlQuery &query)
{
	const QList<QByteArray> parts = requestLine.trimmed().split(' ');
	if (parts.size() < 2 || parts[0] != "GET")
		return QString();
	const QUrl url(QString::fromLatin1(parts[1]));
	query = QUrlQuery(url);
	return url.path();
}

QByteArray formBody(const QList<QPair<QString, QString>> &fields)
{
	QByteArray out;
	for (const auto &f : fields) {
		if (!out.isEmpty())
			out += '&';
		out += QUrl::toPercentEncoding(f.first) + '=' + QUrl::toPercentEncoding(f.second);
	}
	return out;
}

} // namespace OAuthUtil
