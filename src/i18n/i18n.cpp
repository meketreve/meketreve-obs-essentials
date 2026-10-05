/*
Meketreve OBS Essentials - Languages
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

#include "i18n.hpp"

#include <QStringList>

namespace I18n {

namespace {

QString oneOf(const QString &value, const QStringList &allowed)
{
	return allowed.contains(value) ? value : allowed.first();
}

} // namespace

Settings parseSettings(const QJsonObject &json)
{
	Settings s;
	s.ui = oneOf(json.value(QStringLiteral("ui")).toString(),
		     {QStringLiteral("auto"), QStringLiteral("pt"), QStringLiteral("en")});
	s.stream =
		oneOf(json.value(QStringLiteral("stream")).toString(),
		      {QStringLiteral("plugin"), QStringLiteral("pt"), QStringLiteral("en"), QStringLiteral("both")});
	return s;
}

QJsonObject toJson(const Settings &settings)
{
	return {{QStringLiteral("ui"), settings.ui}, {QStringLiteral("stream"), settings.stream}};
}

QString pluginLanguage(const QString &ui, const QString &obsLocale)
{
	if (ui == QLatin1String("pt") || ui == QLatin1String("en"))
		return ui;
	return obsLocale.startsWith(QLatin1String("pt")) ? QStringLiteral("pt") : QStringLiteral("en");
}

QString streamLanguage(const QString &stream, const QString &plugin)
{
	if (stream == QLatin1String("pt") || stream == QLatin1String("en") || stream == QLatin1String("both"))
		return stream;
	return plugin == QLatin1String("pt") ? QStringLiteral("pt") : QStringLiteral("en");
}

QString localeOf(const QString &lang)
{
	return lang == QLatin1String("pt") ? QStringLiteral("pt-BR") : QStringLiteral("en-US");
}

QHash<QString, QString> parseIni(const QByteArray &data)
{
	QHash<QString, QString> texts;
	for (const QByteArray &raw : data.split('\n')) {
		const QString line = QString::fromUtf8(raw).trimmed();
		const qsizetype eq = line.indexOf(QLatin1Char('='));
		if (eq <= 0 || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
			continue;
		QString value = line.mid(eq + 1).trimmed();
		if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
			value = value.mid(1, value.size() - 2);
		value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
		value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
		texts.insert(line.left(eq).trimmed(), value);
	}
	return texts;
}

void Catalog::set(const QString &lang, QHash<QString, QString> table)
{
	(lang == QLatin1String("pt") ? m_pt : m_en) = std::move(table);
}

const QHash<QString, QString> &Catalog::table(const QString &lang) const
{
	return lang == QLatin1String("pt") ? m_pt : m_en;
}

QString Catalog::text(const QString &lang, const QString &key) const
{
	const auto own = table(lang).constFind(key);
	if (own != table(lang).constEnd())
		return own.value();
	return m_en.value(key, key);
}

QString Catalog::both(const QString &key, const QString &separator) const
{
	const QString pt = text(QStringLiteral("pt"), key);
	const QString en = text(QStringLiteral("en"), key);
	return pt == en ? pt : pt + separator + en;
}

} // namespace I18n
