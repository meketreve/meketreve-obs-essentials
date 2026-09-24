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
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrlQuery>

namespace OAuthUtil {

/* RFC 7636: 43-128 chars from the unreserved set. */
QByteArray newCodeVerifier();
QByteArray codeChallengeS256(const QByteArray &verifier);
QByteArray newState();

/* First line of an HTTP request ("GET /callback?code=..&state=.. HTTP/1.1"):
 * returns the path and fills the query; empty path if it is not a GET. */
QString parseRequestLine(const QByteArray &requestLine, QUrlQuery &query);

QByteArray formBody(const QList<QPair<QString, QString>> &fields);

} // namespace OAuthUtil
