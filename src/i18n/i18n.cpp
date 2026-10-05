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

#include <QJsonArray>
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

QString compose(const QString &stream, const Catalog &catalog, const std::function<QString(const Text &)> &build,
		const QString &separator)
{
	const auto textIn = [&catalog](const QString &lang) -> Text {
		return [&catalog, lang](const char *key) {
			return catalog.text(lang, QString::fromUtf8(key));
		};
	};
	if (stream != QLatin1String("both"))
		return build(textIn(stream == QLatin1String("pt") ? QStringLiteral("pt") : QStringLiteral("en")));
	const QString pt = build(textIn(QStringLiteral("pt")));
	const QString en = build(textIn(QStringLiteral("en")));
	return pt == en ? pt : pt + separator + en;
}

QJsonValue swapDefaults(const QJsonValue &current, const QList<QJsonValue> &candidates, const QJsonValue &target)
{
	if (current.isString() && target.isString()) {
		if (current == target)
			return current;
		for (const QJsonValue &c : candidates)
			if (c.isString() && c == current)
				return target;
		return current;
	}
	if (current.isObject() && target.isObject()) {
		QJsonObject out = current.toObject();
		const QJsonObject t = target.toObject();
		for (auto it = out.begin(); it != out.end(); ++it) {
			if (!t.contains(it.key()))
				continue;
			QList<QJsonValue> inner;
			for (const QJsonValue &c : candidates)
				inner.append(c.toObject().value(it.key()));
			it.value() = swapDefaults(it.value(), inner, t.value(it.key()));
		}
		return out;
	}
	if (current.isArray() && target.isArray()) {
		QJsonArray out = current.toArray();
		const QJsonArray t = target.toArray();
		for (qsizetype i = 0; i < out.size() && i < t.size(); i++) {
			QList<QJsonValue> inner;
			for (const QJsonValue &c : candidates)
				inner.append(c.toArray().at(i));
			out[i] = swapDefaults(out.at(i), inner, t.at(i));
		}
		return out;
	}
	return current;
}

} // namespace I18n
