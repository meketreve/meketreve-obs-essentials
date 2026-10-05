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

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>

/* Two languages with two readers: the plugin language is for whoever runs
 * OBS (docks, dialogs, web panel); the stream language is for viewers
 * (overlays, announcements, TTS). Languages are short codes: "pt" or "en",
 * and the stream may also be "both" (bilingual). */
namespace I18n {

/* language.json: ui = auto|pt|en, stream = plugin|pt|en|both. */
struct Settings {
	QString ui = QStringLiteral("auto");
	QString stream = QStringLiteral("plugin");
};

Settings parseSettings(const QJsonObject &json);
QJsonObject toJson(const Settings &settings);

/* "auto" follows OBS: Portuguese OBS -> pt, any other language -> en. */
QString pluginLanguage(const QString &ui, const QString &obsLocale);
/* "plugin" follows the plugin language. Returns pt, en or both. */
QString streamLanguage(const QString &stream, const QString &plugin);

/* "pt" -> "pt-BR", anything else -> "en-US" (the .ini file names). */
QString localeOf(const QString &lang);

/* Key="value" lines of an OBS locale file. */
QHash<QString, QString> parseIni(const QByteArray &data);

/* Both tables, looked up by language with English as the fallback. */
class Catalog {
public:
	void set(const QString &lang, QHash<QString, QString> table);
	const QHash<QString, QString> &table(const QString &lang) const;
	/* Missing in <lang> -> English -> the key itself. */
	QString text(const QString &lang, const QString &key) const;
	/* Bilingual "pt / en" when both differ, a single text otherwise. */
	QString both(const QString &key, const QString &separator = QStringLiteral(" / ")) const;

private:
	QHash<QString, QString> m_pt;
	QHash<QString, QString> m_en;
};

/* A text function for one language: key -> translated template. */
using Text = std::function<QString(const char *key)>;

/* Builds a text for the stream language with <build>; bilingual ("both")
 * builds it in pt and in en and joins them ("pt / en") when they differ. */
QString compose(const QString &stream, const Catalog &catalog, const std::function<QString(const Text &)> &build,
		const QString &separator = QStringLiteral(" / "));

/* Runtime (inside OBS): read once, on the first call, from language.json
 * and the module's locale files. Changes only apply after restarting OBS. */
QString plugin();
QString stream();
/* The stream language for a single-language spot (TTS, plain replies):
 * "both" falls back to the plugin language. */
QString streamSingle();
QString text(const QString &lang, const char *key);
const Catalog &catalog();
Text textFor(const QString &lang);
/* compose() with the configured stream language. */
QString forStream(const std::function<QString(const Text &)> &build, const QString &separator = QStringLiteral(" / "));
/* One template in the stream language (bilingual = "pt / en"). */
QString streamText(const char *key);
/* Saved settings, read again from disk (what the next start will use). */
Settings saved();
bool save(const Settings &settings);

/* The Tools menu dialog (modal). */
void openDialog();

} // namespace I18n
