/*
Meketreve OBS Essentials - Alerts
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
#include "alert-logic.hpp"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Alerts {

namespace {

struct TypeDefault {
	const char *type;
	const char *textKey;
	const char *image;
	const char *sound;
	const char *accent;
	bool enabled;
	int duration;
};

/* The default kit: drawings and sounds made by the overlay page itself. */
constexpr TypeDefault kDefaults[] = {
	{"follow", "Alerts.Default.Follow", "builtin:star", "builtin:pop", "#4FC3F7", true, 5},
	{"sub", "Alerts.Default.Sub", "builtin:crown", "builtin:fanfare", "#FFD54F", true, 7},
	{"resub", "Alerts.Default.Resub", "builtin:crown", "builtin:fanfare", "#FFB300", true, 7},
	{"giftsub", "Alerts.Default.GiftSub", "builtin:gifts", "builtin:levelup", "#F06292", true, 7},
	{"bits", "Alerts.Default.Bits", "builtin:gem", "builtin:coins", "#B388FF", true, 6},
	{"donation", "Alerts.Default.Donation", "builtin:coins", "builtin:coins", "#66BB6A", true, 8},
	{"raid", "Alerts.Default.Raid", "builtin:rocket", "builtin:whoosh", "#FF7043", true, 8},
	{"membership", "Alerts.Default.Membership", "builtin:medal", "builtin:fanfare", "#26A69A", true, 7},
	{"gift", "Alerts.Default.Gift", "builtin:gift", "builtin:sparkle", "#FF4081", true, 5},
	{"share", "Alerts.Default.Share", "builtin:share", "builtin:chime", "#29B6F6", false, 4},
	{"like", "Alerts.Default.Like", "builtin:heart", "builtin:pop", "#EF5350", false, 4},
};

const QStringList kPositions = {
	QStringLiteral("top-left"),    QStringLiteral("top-center"),    QStringLiteral("top-right"),
	QStringLiteral("middle-left"), QStringLiteral("middle-center"), QStringLiteral("middle-right"),
	QStringLiteral("bottom-left"), QStringLiteral("bottom-center"), QStringLiteral("bottom-right"),
};
const QStringList kAnimIn = {QStringLiteral("bounce"),      QStringLiteral("fade"),     QStringLiteral("zoom"),
			     QStringLiteral("slide-down"),  QStringLiteral("slide-up"), QStringLiteral("slide-left"),
			     QStringLiteral("slide-right"), QStringLiteral("none")};
const QStringList kAnimOut = {QStringLiteral("fade"), QStringLiteral("zoom"), QStringLiteral("slide-up"),
			      QStringLiteral("slide-down"), QStringLiteral("none")};
const QStringList kLayouts = {QStringLiteral("above"), QStringLiteral("side"), QStringLiteral("text")};
const QStringList kProviders = {QStringLiteral("giphy"), QStringLiteral("tenor")};

const QStringList kImageExt = {QStringLiteral("gif"), QStringLiteral("png"), QStringLiteral("jpg"),
			       QStringLiteral("jpeg"), QStringLiteral("webp")};
const QStringList kVideoExt = {QStringLiteral("webm"), QStringLiteral("mp4")};
const QStringList kAudioExt = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg")};

QString str(const QJsonObject &o, const char *key, const QString &fallback, qsizetype maxLength = 300)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isString() ? v.toString().left(maxLength) : fallback;
}

QString choice(const QJsonObject &o, const char *key, const QStringList &allowed)
{
	const QString v = o.value(QLatin1String(key)).toString();
	return allowed.contains(v) ? v : allowed.first();
}

double number(const QJsonObject &o, const char *key, double fallback, double low, double high)
{
	const QJsonValue v = o.value(QLatin1String(key));
	const double n = v.isDouble() ? v.toDouble() : fallback;
	return std::isfinite(n) ? std::clamp(n, low, high) : fallback;
}

bool flag(const QJsonObject &o, const char *key, bool fallback)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() ? v.toBool() : fallback;
}

QString color(const QJsonObject &o, const char *key, const QString &fallback)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString v = o.value(QLatin1String(key)).toString();
	return re.match(v).hasMatch() ? v : fallback;
}

QJsonObject typeDefaults(const TypeDefault &d, const TextLookup &text)
{
	return QJsonObject{
		{QStringLiteral("enabled"), d.enabled},
		{QStringLiteral("text"), text(d.textKey)},
		{QStringLiteral("showMessage"), true},
		{QStringLiteral("image"), QLatin1String(d.image)},
		{QStringLiteral("imageSize"), 220},
		{QStringLiteral("sound"), QLatin1String(d.sound)},
		{QStringLiteral("volume"), 100},
		{QStringLiteral("duration"), d.duration},
		{QStringLiteral("min"), 0},
		{QStringLiteral("tts"), false},
		{QStringLiteral("layout"), QStringLiteral("above")},
		{QStringLiteral("animIn"), QStringLiteral("bounce")},
		{QStringLiteral("animOut"), QStringLiteral("fade")},
		{QStringLiteral("font"), QStringLiteral("Poppins")},
		{QStringLiteral("fontSize"), 44},
		{QStringLiteral("textColor"), QStringLiteral("#FFFFFF")},
		{QStringLiteral("accent"), QLatin1String(d.accent)},
	};
}

QJsonObject normalizeType(const QJsonObject &in, const QJsonObject &def)
{
	QJsonObject out;
	out.insert(QStringLiteral("enabled"), flag(in, "enabled", def.value(QStringLiteral("enabled")).toBool()));
	out.insert(QStringLiteral("text"), str(in, "text", def.value(QStringLiteral("text")).toString()));
	out.insert(QStringLiteral("showMessage"), flag(in, "showMessage", true));
	const QJsonValue image = in.value(QStringLiteral("image"));
	out.insert(QStringLiteral("image"),
		   image.isString() ? cleanReference(image.toString()) : def.value(QStringLiteral("image")));
	out.insert(QStringLiteral("imageSize"), std::round(number(in, "imageSize", 220, 0, 800)));
	const QJsonValue sound = in.value(QStringLiteral("sound"));
	out.insert(QStringLiteral("sound"),
		   sound.isString() ? cleanReference(sound.toString()) : def.value(QStringLiteral("sound")));
	out.insert(QStringLiteral("volume"), std::round(number(in, "volume", 100, 0, 100)));
	out.insert(QStringLiteral("duration"),
		   number(in, "duration", def.value(QStringLiteral("duration")).toDouble(), 1, 60));
	out.insert(QStringLiteral("min"), number(in, "min", 0, 0, 1e9));
	out.insert(QStringLiteral("tts"), flag(in, "tts", false));
	out.insert(QStringLiteral("layout"), choice(in, "layout", kLayouts));
	out.insert(QStringLiteral("animIn"), choice(in, "animIn", kAnimIn));
	out.insert(QStringLiteral("animOut"), choice(in, "animOut", kAnimOut));
	out.insert(QStringLiteral("font"), str(in, "font", QStringLiteral("Poppins"), 60).trimmed());
	out.insert(QStringLiteral("fontSize"), std::round(number(in, "fontSize", 44, 12, 160)));
	out.insert(QStringLiteral("textColor"), color(in, "textColor", QStringLiteral("#FFFFFF")));
	out.insert(QStringLiteral("accent"), color(in, "accent", def.value(QStringLiteral("accent")).toString()));
	return out;
}

QString formatCount(double n)
{
	return QString::number(static_cast<qint64>(std::llround(n)));
}

} // namespace

const QStringList &types()
{
	static const QStringList list = []() {
		QStringList out;
		for (const TypeDefault &d : kDefaults)
			out.append(QLatin1String(d.type));
		return out;
	}();
	return list;
}

bool isType(const QString &type)
{
	return types().contains(type);
}

QString platformKey(ChatPlatform platform)
{
	switch (platform) {
	case ChatPlatform::Twitch:
		return QStringLiteral("twitch");
	case ChatPlatform::YouTube:
		return QStringLiteral("youtube");
	case ChatPlatform::Kick:
		return QStringLiteral("kick");
	case ChatPlatform::TikTok:
		return QStringLiteral("tiktok");
	}
	return QString();
}

double parseMoney(const QString &text)
{
	QString s;
	for (const QChar c : text) {
		if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(','))
			s.append(c);
	}
	if (s.isEmpty())
		return 0;
	const qsizetype lastDot = s.lastIndexOf(QLatin1Char('.'));
	const qsizetype lastComma = s.lastIndexOf(QLatin1Char(','));
	QChar decimal;
	if (lastDot >= 0 && lastComma >= 0) {
		decimal = lastDot > lastComma ? QLatin1Char('.') : QLatin1Char(',');
	} else if (lastDot >= 0 || lastComma >= 0) {
		const QChar sep = lastDot >= 0 ? QLatin1Char('.') : QLatin1Char(',');
		const qsizetype at = std::max(lastDot, lastComma);
		/* One separator with three digits after it groups thousands. */
		if (s.count(sep) == 1 && s.size() - at - 1 != 3)
			decimal = sep;
	}
	QString plain;
	for (qsizetype i = 0; i < s.size(); i++) {
		if (s[i].isDigit())
			plain.append(s[i]);
		else if (s[i] == decimal && i == std::max(lastDot, lastComma))
			plain.append(QLatin1Char('.'));
	}
	return plain.toDouble();
}

int diamondsIn(const QString &detail)
{
	static const QRegularExpression re(QStringLiteral("\\((\\d+)\\s*♦\\)"));
	const QRegularExpressionMatch m = re.match(detail);
	return m.hasMatch() ? m.captured(1).toInt() : 0;
}

Event fromChat(const ChatMessage &msg)
{
	Event e;
	e.name = msg.author;
	e.message = msg.text.trimmed();
	e.platform = platformKey(msg.platform);
	const int count = std::max(1, msg.amount);
	e.value = count;
	e.amount = formatCount(count);
	switch (msg.event) {
	case ChatEvent::None:
		return Event();
	case ChatEvent::Follow:
		e.type = QStringLiteral("follow");
		break;
	case ChatEvent::Sub:
		e.type = msg.amount > 1 ? QStringLiteral("resub") : QStringLiteral("sub");
		e.detail = msg.detail;
		break;
	case ChatEvent::GiftSub:
		e.type = QStringLiteral("giftsub");
		e.detail = msg.detail;
		break;
	case ChatEvent::Bits:
		e.type = QStringLiteral("bits");
		break;
	case ChatEvent::Donation:
		e.type = QStringLiteral("donation");
		e.value = parseMoney(msg.detail);
		e.amount = msg.detail.trimmed();
		break;
	case ChatEvent::Raid:
		e.type = QStringLiteral("raid");
		e.value = std::max(0, msg.amount);
		e.amount = formatCount(e.value);
		break;
	case ChatEvent::Membership:
		e.type = QStringLiteral("membership");
		e.detail = msg.detail;
		break;
	case ChatEvent::Gift: {
		e.type = QStringLiteral("gift");
		/* TikTok says what the gifts are worth; the minimum uses that. */
		const int diamonds = diamondsIn(msg.detail);
		if (diamonds > 0)
			e.value = diamonds;
		QString name = msg.detail;
		name.remove(QRegularExpression(QStringLiteral("\\s*\\(\\d+\\s*♦\\)")));
		e.detail = name.trimmed();
		break;
	}
	case ChatEvent::Like:
		e.type = QStringLiteral("like");
		break;
	case ChatEvent::Share:
		e.type = QStringLiteral("share");
		break;
	}
	return e;
}

Event sample(const QString &type, const TextLookup &text)
{
	Event e;
	e.type = type;
	e.name = QStringLiteral("Texuguito");
	e.platform = QStringLiteral("twitch");
	e.test = true;
	e.value = 1;
	e.amount = QStringLiteral("1");
	if (type == QLatin1String("resub")) {
		e.value = 12;
		e.amount = QStringLiteral("12");
		e.message = text("Alerts.TestMessage");
	} else if (type == QLatin1String("sub")) {
		e.message = text("Alerts.TestMessage");
	} else if (type == QLatin1String("giftsub")) {
		e.value = 5;
		e.amount = QStringLiteral("5");
	} else if (type == QLatin1String("bits")) {
		e.value = 500;
		e.amount = QStringLiteral("500");
		e.message = text("Alerts.TestMessage");
	} else if (type == QLatin1String("donation")) {
		e.platform = QStringLiteral("youtube");
		e.amount = text("Alerts.TestMoney");
		e.value = parseMoney(e.amount);
		e.message = text("Alerts.TestMessage");
	} else if (type == QLatin1String("raid")) {
		e.value = 42;
		e.amount = QStringLiteral("42");
	} else if (type == QLatin1String("membership")) {
		e.platform = QStringLiteral("youtube");
	} else if (type == QLatin1String("gift")) {
		e.platform = QStringLiteral("tiktok");
		e.value = 10;
		e.amount = QStringLiteral("10");
		e.detail = text("Alerts.TestGift");
	} else if (type == QLatin1String("like")) {
		e.platform = QStringLiteral("tiktok");
		e.value = 50;
		e.amount = QStringLiteral("50");
	} else if (type == QLatin1String("share")) {
		e.platform = QStringLiteral("tiktok");
	}
	return e;
}

QJsonObject defaults(const TextLookup &text)
{
	QJsonObject typesObj;
	for (const TypeDefault &d : kDefaults)
		typesObj.insert(QLatin1String(d.type), typeDefaults(d, text));
	return QJsonObject{
		{QStringLiteral("version"), 1},
		{QStringLiteral("position"), QStringLiteral("top-center")},
		{QStringLiteral("margin"), 80},
		{QStringLiteral("gap"), 1},
		{QStringLiteral("volume"), 80},
		{QStringLiteral("types"), typesObj},
		{QStringLiteral("integrations"), QJsonObject{{QStringLiteral("provider"), QStringLiteral("giphy")},
							     {QStringLiteral("giphyKey"), QString()},
							     {QStringLiteral("tenorKey"), QString()}}},
	};
}

QJsonObject normalize(const QJsonObject &config, const TextLookup &text)
{
	const QJsonObject def = defaults(text);
	QJsonObject out;
	out.insert(QStringLiteral("version"), 1);
	const QString position = config.value(QStringLiteral("position")).toString();
	out.insert(QStringLiteral("position"), kPositions.contains(position) ? position : kPositions[1]);
	out.insert(QStringLiteral("margin"), std::round(number(config, "margin", 80, 0, 600)));
	out.insert(QStringLiteral("gap"), number(config, "gap", 1, 0, 30));
	out.insert(QStringLiteral("volume"), std::round(number(config, "volume", 80, 0, 100)));

	const QJsonObject inTypes = config.value(QStringLiteral("types")).toObject();
	const QJsonObject defTypes = def.value(QStringLiteral("types")).toObject();
	QJsonObject outTypes;
	for (const QString &type : types())
		outTypes.insert(type, normalizeType(inTypes.value(type).toObject(), defTypes.value(type).toObject()));
	out.insert(QStringLiteral("types"), outTypes);

	const QJsonObject in = config.value(QStringLiteral("integrations")).toObject();
	out.insert(QStringLiteral("integrations"),
		   QJsonObject{{QStringLiteral("provider"), choice(in, "provider", kProviders)},
			       {QStringLiteral("giphyKey"), str(in, "giphyKey", QString(), 100).trimmed()},
			       {QStringLiteral("tenorKey"), str(in, "tenorKey", QString(), 100).trimmed()}});
	return out;
}

QJsonObject forOverlay(const QJsonObject &config)
{
	QJsonObject out = config;
	out.remove(QStringLiteral("integrations"));
	return out;
}

bool passes(const QJsonObject &config, const Event &event)
{
	const QJsonObject type = config.value(QStringLiteral("types")).toObject().value(event.type).toObject();
	if (type.isEmpty() || !type.value(QStringLiteral("enabled")).toBool())
		return false;
	return event.value + 1e-9 >= type.value(QStringLiteral("min")).toDouble();
}

QJsonObject toJson(const Event &event)
{
	return QJsonObject{
		{QStringLiteral("type"), event.type},       {QStringLiteral("name"), event.name},
		{QStringLiteral("amount"), event.amount},   {QStringLiteral("detail"), event.detail},
		{QStringLiteral("message"), event.message}, {QStringLiteral("platform"), event.platform},
		{QStringLiteral("test"), event.test},
	};
}

QString cleanReference(const QString &value)
{
	const QString v = value.trimmed();
	static const QRegularExpression builtin(QStringLiteral("^builtin:[a-z0-9-]{1,30}$"));
	if (v.isEmpty() || builtin.match(v).hasMatch())
		return v;
	if (v.startsWith(QLatin1String("media:"))) {
		const QString name = v.mid(6);
		return !name.isEmpty() && safeMediaName(name) == name ? v : QString();
	}
	if ((v.startsWith(QLatin1String("https://")) || v.startsWith(QLatin1String("http://"))) && v.size() <= 2000 &&
	    !v.contains(QLatin1Char('"')) && !v.contains(QLatin1Char('\'')) && !v.contains(QLatin1Char(' ')) &&
	    !v.contains(QLatin1Char('<')))
		return v;
	return QString();
}

QString mediaKind(const QString &name)
{
	const QString ext = QFileInfo(name).suffix().toLower();
	if (kImageExt.contains(ext))
		return QStringLiteral("image");
	if (kVideoExt.contains(ext))
		return QStringLiteral("video");
	if (kAudioExt.contains(ext))
		return QStringLiteral("audio");
	return QString();
}

QString safeMediaName(const QString &name)
{
	const QFileInfo info(name.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1));
	const QString ext = info.suffix().toLower();
	if (mediaKind(QStringLiteral("x.") + ext).isEmpty())
		return QString();
	QString base;
	for (const QChar c : info.completeBaseName()) {
		const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
				(c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
				(c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_') ||
				c == QLatin1Char('-');
		if (ok)
			base.append(c);
		else if (!base.endsWith(QLatin1Char('-')))
			base.append(QLatin1Char('-'));
	}
	while (base.startsWith(QLatin1Char('-')))
		base.remove(0, 1);
	base = base.left(60);
	while (base.endsWith(QLatin1Char('-')))
		base.chop(1);
	if (base.isEmpty())
		base = QStringLiteral("file");
	return base + QLatin1Char('.') + ext;
}

bool isLocalHost(const QByteArray &host, quint16 port)
{
	const QByteArray p = QByteArray::number(port);
	return host == "localhost:" + p || host == "127.0.0.1:" + p;
}

bool isLocalOrigin(const QByteArray &origin, quint16 port)
{
	if (origin.isEmpty())
		return true;
	const QByteArray p = QByteArray::number(port);
	return origin == "http://localhost:" + p || origin == "http://127.0.0.1:" + p;
}

bool GiftDedup::swallow(const Event &event, qint64 nowMs)
{
	if (event.type != QLatin1String("giftsub"))
		return false;
	for (auto it = m_pending.begin(); it != m_pending.end();) {
		if (it->until < nowMs || it->left <= 0)
			it = m_pending.erase(it);
		else
			++it;
	}
	const QString key = event.platform + QLatin1Char('/') + event.name.toLower();
	const int count = static_cast<int>(event.value);
	if (count > 1) {
		m_pending.insert(key, Pending{count, nowMs + 60000});
		return false;
	}
	auto it = m_pending.find(key);
	if (it == m_pending.end())
		return false;
	it->left--;
	return true;
}

} // namespace Alerts
