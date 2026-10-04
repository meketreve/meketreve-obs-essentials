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

#include "chat-overlay.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace ChatOverlay {

namespace {

const char *const kDefaultHidden = "nightbot, streamelements, streamlabs, moobot, fossabot, botrix, kicklet";

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

/* "@Fulano " -> "fulano": YouTube names come with the @. */
QString plainName(const QString &name)
{
	QString s = name.trimmed().toLower();
	while (s.startsWith(QLatin1Char('@')))
		s.remove(0, 1);
	return s;
}

} // namespace

QStringList platforms()
{
	return {QStringLiteral("twitch"), QStringLiteral("youtube"), QStringLiteral("kick")};
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
	}
	return QString();
}

QJsonObject defaults()
{
	return normalize(QJsonObject());
}

QJsonObject normalize(const QJsonObject &stored)
{
	const QJsonObject inPlatforms = stored.value(QStringLiteral("platforms")).toObject();
	QJsonObject outPlatforms;
	for (const QString &p : platforms()) {
		const QJsonValue v = inPlatforms.value(p);
		outPlatforms.insert(p, v.isBool() ? v.toBool() : true);
	}
	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	const QJsonValue hidden = stored.value(QStringLiteral("hideUsers"));
	return QJsonObject{
		{QStringLiteral("platforms"), outPlatforms},
		{QStringLiteral("hideCommands"), flag(stored, "hideCommands", true)},
		{QStringLiteral("hideUsers"),
		 hidden.isString() ? hidden.toString().left(500) : QString::fromLatin1(kDefaultHidden)},
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("fontSize"), number(stored, "fontSize", 26, 12, 72)},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), number(stored, "bubbleOpacity", 45, 0, 100)},
		{QStringLiteral("shadow"), flag(stored, "shadow", true)},
		{QStringLiteral("showPlatform"), flag(stored, "showPlatform", true)},
		{QStringLiteral("showBadges"), flag(stored, "showBadges", true)},
		{QStringLiteral("newest"), choice(stored, "newest", {QStringLiteral("bottom"), QStringLiteral("top")})},
		{QStringLiteral("animation"),
		 choice(stored, "animation",
			{QStringLiteral("slide"), QStringLiteral("fade"), QStringLiteral("none")})},
		{QStringLiteral("maxMessages"), number(stored, "maxMessages", 15, 1, 50)},
		{QStringLiteral("fadeAfter"), number(stored, "fadeAfter", 0, 0, 600)},
	};
}

bool passes(const QJsonObject &config, const ChatMessage &msg)
{
	if (msg.event != ChatEvent::None || msg.text.trimmed().isEmpty())
		return false;
	if (config.value(QStringLiteral("hideCommands")).toBool() && msg.text.trimmed().startsWith(QLatin1Char('!')))
		return false;
	const QString author = plainName(msg.author);
	const QStringList hidden = config.value(QStringLiteral("hideUsers")).toString().split(QLatin1Char(','));
	for (const QString &name : hidden) {
		if (!name.trimmed().isEmpty() && plainName(name) == author)
			return false;
	}
	return true;
}

QJsonObject toJson(const ChatMessage &msg)
{
	QJsonArray emotes;
	for (const ChatEmote &e : msg.emotes) {
		if (e.start < 0 || e.length <= 0 || e.start + e.length > msg.text.size())
			continue;
		emotes.append(QJsonObject{{QStringLiteral("start"), static_cast<qint64>(e.start)},
					  {QStringLiteral("length"), static_cast<qint64>(e.length)},
					  {QStringLiteral("url"), e.url}});
	}
	return QJsonObject{{QStringLiteral("id"), msg.id},
			   {QStringLiteral("user"), msg.userId},
			   {QStringLiteral("platform"), platformKey(msg.platform)},
			   {QStringLiteral("author"), msg.author},
			   {QStringLiteral("color"), msg.authorColor},
			   {QStringLiteral("text"), msg.text},
			   {QStringLiteral("highlight"), msg.highlight},
			   {QStringLiteral("mod"), msg.isMod},
			   {QStringLiteral("sub"), msg.isSub},
			   {QStringLiteral("broadcaster"), msg.isBroadcaster},
			   {QStringLiteral("emotes"), emotes}};
}

QJsonArray samples()
{
	const auto line = [](ChatPlatform platform, const char *author, const char *color, const char *text) {
		ChatMessage msg{platform, QString::fromUtf8(author), QString::fromLatin1(color),
				QString::fromUtf8(text), QString()};
		return msg;
	};
	ChatMessage kappa = line(ChatPlatform::Twitch, "Texuguito", "#9146FF", "boa noite chat Kappa");
	kappa.emotes.append(
		ChatEmote{15, 5, QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/2.0")});
	kappa.isSub = true;
	ChatMessage owner = line(ChatPlatform::Kick, "Meketreve", "#53FC18", "bem-vindos à live!");
	owner.isBroadcaster = true;
	ChatMessage mod = line(ChatPlatform::YouTube, "@Moderadora", "", "lembrem de seguir as regras 🙂");
	mod.isMod = true;
	QJsonArray out;
	for (const ChatMessage &msg :
	     {kappa, line(ChatPlatform::YouTube, "@Fulana", "", "cheguei agora, o que perdi?"), owner, mod,
	      line(ChatPlatform::Twitch, "Ciclano", "#FF7F50", "essa jogada foi absurda")})
		out.append(toJson(msg));
	return out;
}

} // namespace ChatOverlay
