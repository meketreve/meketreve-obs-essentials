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
#include "bot-data.hpp"
#include "bot-colors.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace BotData {

namespace {

const QStringList kPalette{QStringLiteral("#e74c3c"), QStringLiteral("#3498db"), QStringLiteral("#2ecc71"),
			   QStringLiteral("#f1c40f"), QStringLiteral("#9b59b6"), QStringLiteral("#1abc9c"),
			   QStringLiteral("#e67e22"), QStringLiteral("#34495e")};

/* The canonical value stays in Portuguese: it is also the sprite key in
 * web/assets/lpc/manifest.json. */
const QHash<QString, QString> &hatAliases()
{
	static const QHash<QString, QString> aliases{
		{QStringLiteral("cap"), QStringLiteral("boné")},
		{QStringLiteral("crown"), QStringLiteral("coroa")},
		{QStringLiteral("horns"), QStringLiteral("chifres")},
		{QStringLiteral("tricorne"), QStringLiteral("tricornio")},
		{QStringLiteral("bicorn"), QStringLiteral("bicorne")},
		{QStringLiteral("tophat"), QStringLiteral("cartola")},
		{QStringLiteral("top hat"), QStringLiteral("cartola")},
		{QStringLiteral("coconut"), QStringLiteral("coco")},
		{QStringLiteral("santa"), QStringLiteral("natalino")},
		{QStringLiteral("christmas"), QStringLiteral("natalino")},
		{QStringLiteral("wizard"), QStringLiteral("mago")},
		{QStringLiteral("helmet"), QStringLiteral("elmo")},
		{QStringLiteral("legionary"), QStringLiteral("legionario")},
		{QStringLiteral("hood"), QStringLiteral("capuz")},
		{QStringLiteral("headband"), QStringLiteral("faixa")},
		{QStringLiteral("none"), QStringLiteral("nenhum")},
	};
	return aliases;
}

const QHash<QString, QString> &accessoryAliases()
{
	static const QHash<QString, QString> aliases{
		{QStringLiteral("glasses"), QStringLiteral("óculos")},
		{QStringLiteral("cape"), QStringLiteral("capa")},
		{QStringLiteral("wings"), QStringLiteral("asas")},
		{QStringLiteral("necklace"), QStringLiteral("colar")},
		{QStringLiteral("scarf"), QStringLiteral("cachecol")},
		{QStringLiteral("bow"), QStringLiteral("laco")},
		{QStringLiteral("eyepatch"), QStringLiteral("tapaolho")},
		{QStringLiteral("sunglasses"), QStringLiteral("oculosescuros")},
		{QStringLiteral("monocle"), QStringLiteral("monoculo")},
		{QStringLiteral("batwings"), QStringLiteral("asasmorcego")},
		{QStringLiteral("bat wings"), QStringLiteral("asasmorcego")},
		{QStringLiteral("butterflywings"), QStringLiteral("asasborboleta")},
		{QStringLiteral("butterfly wings"), QStringLiteral("asasborboleta")},
		{QStringLiteral("dragonflywings"), QStringLiteral("asaslibelula")},
		{QStringLiteral("dragonfly wings"), QStringLiteral("asaslibelula")},
		{QStringLiteral("none"), QStringLiteral("nenhum")},
	};
	return aliases;
}

std::pair<bool, QString> validateOption(const QString &raw, const QStringList &options,
					const QHash<QString, QString> &aliases)
{
	QString value = raw.trimmed().toLower();
	value = aliases.value(value, value);
	if (!options.contains(value))
		return {false, QString()};
	return {true, value == QLatin1String("nenhum") ? QString() : value};
}

} // namespace

const QStringList &hats()
{
	static const QStringList list{
		QStringLiteral("boné"),       QStringLiteral("coroa"),   QStringLiteral("chifres"),
		QStringLiteral("tricornio"),  QStringLiteral("bicorne"), QStringLiteral("cartola"),
		QStringLiteral("tiara"),      QStringLiteral("coco"),    QStringLiteral("natalino"),
		QStringLiteral("mago"),       QStringLiteral("viking"),  QStringLiteral("elmo"),
		QStringLiteral("legionario"), QStringLiteral("bandana"), QStringLiteral("capuz"),
		QStringLiteral("faixa"),      QStringLiteral("nenhum"),
	};
	return list;
}

const QStringList &accessories()
{
	static const QStringList list{
		QStringLiteral("óculos"),      QStringLiteral("capa"),          QStringLiteral("asas"),
		QStringLiteral("colar"),       QStringLiteral("cachecol"),      QStringLiteral("laco"),
		QStringLiteral("tapaolho"),    QStringLiteral("oculosescuros"), QStringLiteral("monoculo"),
		QStringLiteral("asasmorcego"), QStringLiteral("asasborboleta"), QStringLiteral("asaslibelula"),
		QStringLiteral("nenhum"),
	};
	return list;
}

static QStringList optionNames(const QStringList &options, const QHash<QString, QString> &aliases, bool english)
{
	if (!english)
		return options;
	QStringList names;
	for (const QString &option : options) {
		QString best;
		for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
			const QString &alias = it.key();
			if (it.value() != option || alias.contains(QLatin1Char(' ')))
				continue;
			if (best.isEmpty() || alias.size() < best.size() ||
			    (alias.size() == best.size() && alias < best))
				best = alias;
		}
		names.append(best.isEmpty() ? option : best);
	}
	return names;
}

QStringList hatNames(bool english)
{
	return optionNames(hats(), hatAliases(), english);
}

QStringList accessoryNames(bool english)
{
	return optionNames(accessories(), accessoryAliases(), english);
}

QJsonObject readJsonFile(const QString &path)
{
	QFile file(path);
	if (!file.exists() || !file.open(QIODevice::ReadOnly))
		return QJsonObject();
	QJsonParseError error{};
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
	file.close();
	if (error.error != QJsonParseError::NoError || !doc.isObject()) {
		const QString corrupt = path + QStringLiteral(".corrupt");
		QFile::remove(corrupt);
		QFile::rename(path, corrupt);
		return QJsonObject();
	}
	return doc.object();
}

bool writeJsonFile(const QString &path, const QJsonObject &obj)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
	return file.commit();
}

QString defaultColorFor(const QString &key)
{
	const QByteArray digest = QCryptographicHash::hash(key.toLower().toUtf8(), QCryptographicHash::Sha256);
	const auto index = static_cast<quint8>(digest[0]) % kPalette.size();
	return kPalette[index];
}

std::optional<QString> validateColor(const QString &raw)
{
	const QString value = raw.trimmed();
	static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	if (hex.match(value).hasMatch())
		return value.toLower();
	const QByteArray lowered = value.toLower().toUtf8();
	for (const auto &entry : colorNames()) {
		if (lowered == entry.first)
			return QString::fromLatin1(entry.second);
	}
	return std::nullopt;
}

std::pair<bool, QString> validateHat(const QString &raw)
{
	return validateOption(raw, hats(), hatAliases());
}

std::pair<bool, QString> validateAccessory(const QString &raw)
{
	return validateOption(raw, accessories(), accessoryAliases());
}

QString validateNick(const QString &raw)
{
	/* Letters, digits, underscore, spaces and Latin-1 accents, like the
	 * Python version's [\w À-ÿ]. */
	static const QRegularExpression notAllowed(QStringLiteral("[^\\w À-ÿ]"),
						   QRegularExpression::UseUnicodePropertiesOption);
	QString cleaned = raw;
	cleaned.remove(notAllowed);
	return cleaned.trimmed().left(16).trimmed();
}

} // namespace BotData

namespace {

QString nowIso()
{
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace

ViewerStore::ViewerStore(const QString &path) : m_path(path)
{
	load();
}

void ViewerStore::load()
{
	m_viewers.clear();
	const QJsonObject raw = BotData::readJsonFile(m_path);
	for (auto it = raw.begin(); it != raw.end(); ++it) {
		const QJsonObject o = it.value().toObject();
		Viewer v;
		v.cor = o.value(QStringLiteral("cor")).toString(BotData::defaultColorFor(it.key()));
		v.chapeu = o.value(QStringLiteral("chapeu")).toString();
		v.acessorio = o.value(QStringLiteral("acessorio")).toString();
		v.nick = o.value(QStringLiteral("nick")).toString();
		v.exibicao = o.value(QStringLiteral("exibicao")).toString();
		v.primeiraVez = o.value(QStringLiteral("primeira_vez")).toString();
		v.ultimaVez = o.value(QStringLiteral("ultima_vez")).toString();
		m_viewers.insert(it.key().toLower(), v);
	}
}

void ViewerStore::save() const
{
	QJsonObject raw;
	for (auto it = m_viewers.constBegin(); it != m_viewers.constEnd(); ++it) {
		const Viewer &v = it.value();
		const auto optional = [](const QString &s) {
			return s.isEmpty() ? QJsonValue() : QJsonValue(s);
		};
		QJsonObject o{{QStringLiteral("cor"), v.cor},
			      {QStringLiteral("chapeu"), optional(v.chapeu)},
			      {QStringLiteral("acessorio"), optional(v.acessorio)},
			      {QStringLiteral("nick"), optional(v.nick)},
			      {QStringLiteral("primeira_vez"), v.primeiraVez},
			      {QStringLiteral("ultima_vez"), v.ultimaVez}};
		if (!v.exibicao.isEmpty())
			o.insert(QStringLiteral("exibicao"), v.exibicao);
		raw.insert(it.key(), o);
	}
	BotData::writeJsonFile(m_path, raw);
}

Viewer &ViewerStore::getOrCreate(const QString &key, const QString &displayName)
{
	const QString k = key.toLower();
	auto it = m_viewers.find(k);
	if (it == m_viewers.end()) {
		Viewer v;
		v.cor = BotData::defaultColorFor(k);
		v.exibicao = displayName;
		v.primeiraVez = v.ultimaVez = nowIso();
		it = m_viewers.insert(k, v);
		save();
	} else if (!displayName.isEmpty() && it->exibicao != displayName) {
		it->exibicao = displayName;
	}
	return it.value();
}

void ViewerStore::touch(Viewer &v) const
{
	v.ultimaVez = nowIso();
	save();
}

void ViewerStore::setColor(const QString &key, const QString &cor)
{
	Viewer &v = getOrCreate(key);
	v.cor = cor;
	touch(v);
}

void ViewerStore::resetColor(const QString &key)
{
	setColor(key, BotData::defaultColorFor(key));
}

void ViewerStore::setHat(const QString &key, const QString &hat)
{
	Viewer &v = getOrCreate(key);
	v.chapeu = hat;
	touch(v);
}

void ViewerStore::setAccessory(const QString &key, const QString &accessory)
{
	Viewer &v = getOrCreate(key);
	v.acessorio = accessory;
	touch(v);
}

void ViewerStore::setNick(const QString &key, const QString &nick)
{
	Viewer &v = getOrCreate(key);
	v.nick = nick;
	touch(v);
}

PointsStore::PointsStore(const QString &path) : m_path(path)
{
	load();
}

void PointsStore::load()
{
	m_points.clear();
	const QJsonObject raw = BotData::readJsonFile(m_path);
	for (auto it = raw.begin(); it != raw.end(); ++it)
		m_points.insert(it.key().toLower(), static_cast<qint64>(it.value().toDouble()));
}

void PointsStore::save() const
{
	QJsonObject raw;
	for (auto it = m_points.constBegin(); it != m_points.constEnd(); ++it)
		raw.insert(it.key(), static_cast<double>(it.value()));
	BotData::writeJsonFile(m_path, raw);
}

void PointsStore::addMany(const QSet<QString> &keys, qint64 amount)
{
	if (keys.isEmpty())
		return;
	for (const QString &key : keys) {
		const QString k = key.toLower();
		m_points[k] = m_points.value(k, 0) + amount;
	}
	save();
}

bool PointsStore::spend(const QString &key, qint64 amount)
{
	const QString k = key.toLower();
	const qint64 current = m_points.value(k, 0);
	if (current < amount)
		return false;
	m_points[k] = current - amount;
	save();
	return true;
}

CustomCommandStore::CustomCommandStore(const QString &path) : m_path(path)
{
	load();
}

void CustomCommandStore::load()
{
	m_commands.clear();
	const QJsonObject raw = BotData::readJsonFile(m_path);
	for (auto it = raw.begin(); it != raw.end(); ++it)
		m_commands.insert(it.key().toLower(), it.value().toVariant().toString());
}

void CustomCommandStore::save() const
{
	QJsonObject raw;
	for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it)
		raw.insert(it.key(), it.value());
	BotData::writeJsonFile(m_path, raw);
}

QStringList CustomCommandStore::names() const
{
	QStringList list = m_commands.keys();
	list.sort();
	return list;
}

void CustomCommandStore::set(const QString &name, const QString &response)
{
	m_commands.insert(name.toLower(), response);
	save();
}

bool CustomCommandStore::remove(const QString &name)
{
	if (m_commands.remove(name.toLower()) == 0)
		return false;
	save();
	return true;
}

Raffle::Raffle(Chooser choose) : m_choose(std::move(choose))
{
	if (!m_choose) {
		m_choose = [](const QStringList &list) {
			return list.at(static_cast<qsizetype>(
				QRandomGenerator::global()->bounded(static_cast<quint32>(list.size()))));
		};
	}
}

bool Raffle::start(qint64 prize)
{
	if (m_active)
		return false;
	m_active = true;
	m_prize = prize;
	m_participants.clear();
	return true;
}

bool Raffle::join(const QString &key)
{
	const QString k = key.toLower();
	if (!m_active || m_participants.contains(k))
		return false;
	m_participants.insert(k);
	return true;
}

std::pair<QString, qint64> Raffle::finish(PointsStore &points)
{
	const qint64 prize = m_prize;
	QStringList participants(m_participants.begin(), m_participants.end());
	participants.sort();
	m_active = false;
	m_prize = 0;
	m_participants.clear();
	if (participants.isEmpty())
		return {QString(), prize};
	const QString winner = m_choose(participants);
	points.add(winner, prize);
	return {winner, prize};
}
