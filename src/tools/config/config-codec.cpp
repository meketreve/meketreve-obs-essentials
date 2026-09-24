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
#include "config-codec.hpp"

#include <QJsonDocument>

namespace ConfigCodec {

namespace {

bool fail(QString *error, const char *why)
{
	if (error)
		*error = QString::fromLatin1(why);
	return false;
}

} // namespace

QString encode(const QJsonObject &bundle)
{
	QJsonObject copy = bundle;
	copy.insert(QStringLiteral("format"), kFormat);
	const QByteArray json = QJsonDocument(copy).toJson(QJsonDocument::Compact);
	const QByteArray packed =
		qCompress(json, 9).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	return QString::fromLatin1(kPrefix) + QString::fromLatin1(packed);
}

bool validate(const QJsonObject &bundle, QString *error)
{
	const int format = bundle.value(QStringLiteral("format")).toInt(0);
	if (format < 1)
		return fail(error, "not a Meketreve configuration");
	if (format > kFormat)
		return fail(error, "made by a newer version of the plugin");
	return true;
}

bool decode(const QString &text, QJsonObject &bundle, QString *error)
{
	/* Chat apps and e-mail like to wrap long lines. */
	QString compact = text;
	compact.remove(QLatin1Char(' ')).remove(QLatin1Char('\n')).remove(QLatin1Char('\r')).remove(QLatin1Char('\t'));

	if (!compact.startsWith(QLatin1String(kPrefix)))
		return fail(error, "the text does not start with MOE1:");

	const auto decoded =
		QByteArray::fromBase64Encoding(compact.mid(static_cast<qsizetype>(qstrlen(kPrefix))).toLatin1(),
					       QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded)
		return fail(error, "the text is damaged (invalid characters)");

	const QByteArray &packed = decoded.decoded;
	if (packed.size() < 4)
		return fail(error, "the text is damaged (too short)");
	/* qCompress stores the uncompressed size up front, big endian. */
	const quint32 declared = (static_cast<quint32>(static_cast<quint8>(packed[0])) << 24) |
				 (static_cast<quint32>(static_cast<quint8>(packed[1])) << 16) |
				 (static_cast<quint32>(static_cast<quint8>(packed[2])) << 8) |
				 static_cast<quint32>(static_cast<quint8>(packed[3]));
	if (declared > kMaxJsonBytes)
		return fail(error, "the configuration is too large");

	const QByteArray json = qUncompress(packed);
	if (json.isEmpty())
		return fail(error, "the text is damaged (cannot decompress)");

	QJsonParseError parseError{};
	const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		return fail(error, "the text is damaged (invalid JSON)");

	if (!validate(doc.object(), error))
		return false;
	bundle = doc.object();
	return true;
}

} // namespace ConfigCodec
