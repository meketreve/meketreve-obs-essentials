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

#include "i18n.h"
#include "i18n.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <mutex>

namespace I18n {

namespace {

struct State {
	QString plugin = QStringLiteral("en");
	QString stream = QStringLiteral("en");
	Catalog catalog;
};

std::once_flag g_once;
State g_state;

QString configFile()
{
	char *path = obs_module_config_path("language.json");
	const QString file = QString::fromUtf8(path ? path : "");
	bfree(path);
	return file;
}

QByteArray moduleFile(const char *name)
{
	char *path = obs_module_file(name);
	QFile file(QString::fromUtf8(path ? path : ""));
	bfree(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void start(const char *obsLocale)
{
	std::call_once(g_once, [obsLocale]() {
		const Settings s = saved();
		g_state.plugin = pluginLanguage(s.ui, QString::fromUtf8(obsLocale ? obsLocale : obs_get_locale()));
		g_state.stream = streamLanguage(s.stream, g_state.plugin);
		g_state.catalog.set(QStringLiteral("en"), parseIni(moduleFile("locale/en-US.ini")));
		g_state.catalog.set(QStringLiteral("pt"), parseIni(moduleFile("locale/pt-BR.ini")));
	});
}

} // namespace

QString plugin()
{
	start(nullptr);
	return g_state.plugin;
}

QString stream()
{
	start(nullptr);
	return g_state.stream;
}

QString streamSingle()
{
	const QString s = stream();
	return s == QLatin1String("both") ? plugin() : s;
}

QString text(const QString &lang, const char *key)
{
	start(nullptr);
	return g_state.catalog.text(lang, QString::fromUtf8(key));
}

const Catalog &catalog()
{
	start(nullptr);
	return g_state.catalog;
}

Settings saved()
{
	QFile file(configFile());
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return parseSettings(QJsonDocument::fromJson(file.readAll()).object());
}

bool save(const Settings &settings)
{
	const QString path = configFile();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	return file.write(QJsonDocument(toJson(settings)).toJson()) > 0;
}

} // namespace I18n

const char *meketreve_i18n_start(const char *obs_locale)
{
	I18n::start(obs_locale);
	return I18n::plugin() == QLatin1String("pt") ? "pt-BR" : "en-US";
}
