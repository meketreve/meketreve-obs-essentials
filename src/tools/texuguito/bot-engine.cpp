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
#include "bot-engine.hpp"

#include <QDir>
#include <QSaveFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kDanceMs = 4000;
constexpr int kCheerMs = 4000;
constexpr int kQuestMs = 5000;
constexpr int kTtsCacheSize = 20;
constexpr int kCustomNameMax = 25;
constexpr int kCustomResponseMax = 400;

const QStringList kAdd{QStringLiteral("add"), QStringLiteral("adicionar"), QStringLiteral("novo")};
const QStringList kEdit{QStringLiteral("edit"), QStringLiteral("editar")};
const QStringList kRemove{QStringLiteral("del"),     QStringLiteral("delete"), QStringLiteral("remove"),
			  QStringLiteral("remover"), QStringLiteral("rm"),     QStringLiteral("apagar")};
const QStringList kList{QStringLiteral("list"), QStringLiteral("lista"), QStringLiteral("listar")};

/* Per site/commands.json: the canonical command in each language. Neutral
 * aliases (ping, status, pts, p, tts, cmd, audio) are in neither set and
 * fall back to the stream language. */
const QSet<QString> kEnglishInvocations{
	QStringLiteral("color"),     QStringLiteral("resetcolor"), QStringLiteral("hat"),
	QStringLiteral("accessory"), QStringLiteral("nick"),       QStringLiteral("nickname"),
	QStringLiteral("dance"),     QStringLiteral("setavatar"),  QStringLiteral("help"),
	QStringLiteral("commands"),  QStringLiteral("command"),    QStringLiteral("points"),
	QStringLiteral("give"),      QStringLiteral("givepoints"), QStringLiteral("addpoints"),
	QStringLiteral("play"),      QStringLiteral("speak"),      QStringLiteral("sounds"),
	QStringLiteral("stop"),      QStringLiteral("reload"),     QStringLiteral("raffle"),
	QStringLiteral("join"),      QStringLiteral("addsound"),   QStringLiteral("quests"),
	QStringLiteral("level"),     QStringLiteral("adventure"),  QStringLiteral("attack"),
	QStringLiteral("boss"),      QStringLiteral("quest"),      QStringLiteral("missions"),
	QStringLiteral("lvl"),       QStringLiteral("colour"),     QStringLiteral("resetcolour"),
	QStringLiteral("enter"),     QStringLiteral("items"),      QStringLiteral("inventory"),
	QStringLiteral("shop"),      QStringLiteral("store"),      QStringLiteral("buy"),
};
const QSet<QString> kPortugueseInvocations{
	QStringLiteral("cor"),
	QStringLiteral("resetcor"),
	QStringLiteral("chapeu"),
	QStringLiteral("acessorio"),
	QStringLiteral("apelido"),
	QStringLiteral("dança"),
	QStringLiteral("danca"),
	QStringLiteral("avatarmod"),
	QStringLiteral("ajuda"),
	QStringLiteral("comandos"),
	QStringLiteral("comando"),
	QStringLiteral("pontos"),
	QStringLiteral("dar"),
	QStringLiteral("darpontos"),
	QStringLiteral("addpontos"),
	QStringLiteral("tocar"),
	QStringLiteral("falar"),
	QStringLiteral("audios"),
	QStringLiteral("sons"),
	QStringLiteral("parar"),
	QStringLiteral("recarregar"),
	QStringLiteral("sorteio"),
	QStringLiteral("entrar"),
	QStringLiteral("addaudio"),
	QStringLiteral("adicionaraudio"),
	QStringLiteral("addsom"),
	QStringLiteral("estado"),
	QStringLiteral("missoes"),
	QStringLiteral("missões"),
	QStringLiteral("nivel"),
	QStringLiteral("nível"),
	QStringLiteral("aventura"),
	QStringLiteral("atacar"),
	QStringLiteral("chefao"),
	QStringLiteral("chefão"),
	QStringLiteral("itens"),
	QStringLiteral("inventario"),
	QStringLiteral("inventário"),
	QStringLiteral("loja"),
	QStringLiteral("comprar"),
};

} // namespace

namespace BotText {

QString truncate(const QString &text, int limit)
{
	if (text.size() <= limit)
		return text;
	return text.left(limit - 3) + QStringLiteral("...");
}

std::pair<QString, QStringList> parseInvocation(const QString &content, bool isReply)
{
	QString text = content;
	if (isReply) {
		const qsizetype space = text.indexOf(QLatin1Char(' '));
		text = space < 0 ? QString() : text.mid(space + 1);
	}
	text = text.trimmed();
	if (!text.startsWith(QLatin1Char('!')))
		return {QString(), {}};
	QStringList words = text.mid(1).trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	if (words.isEmpty())
		return {QString(), {}};
	const QString name = words.takeFirst().toLower();
	return {name, words};
}

} // namespace BotText

std::optional<bool> BotEngine::commandEnglish(const QString &invoked)
{
	if (kEnglishInvocations.contains(invoked))
		return true;
	if (kPortugueseInvocations.contains(invoked))
		return false;
	return std::nullopt;
}

QString BotEngine::tCmd(const QString &invoked, const char *key) const
{
	const std::optional<bool> lang = commandEnglish(invoked);
	if (lang.has_value()) {
		const QHash<QString, QString> &texts = *lang ? m_cmdEn : m_cmdPt;
		const QString found = texts.value(QLatin1String(key));
		if (!found.isEmpty())
			return found;
	}
	return m_reply ? m_reply(key) : t(key);
}

BotEngine::BotEngine(const QString &dataDir, const QString &audioDir, QObject *parent)
	: QObject(parent),
	  m_viewers(dataDir + QStringLiteral("/viewers.json")),
	  m_points(dataDir + QStringLiteral("/points.json")),
	  m_custom(dataDir + QStringLiteral("/custom_commands.json")),
	  m_quests(dataDir + QStringLiteral("/quests.json")),
	  m_boss(dataDir + QStringLiteral("/boss.json")),
	  m_items(dataDir + QStringLiteral("/items.json")),
	  m_audioDir(audioDir),
	  m_library(dataDir + QStringLiteral("/sound-groups.json"))
{
	m_clock.start();
	registerCommands();
	/* The sounds are read by setText(), once names and waits are known. */

	m_pointsTimer.setInterval(kPointsTickSeconds * 1000);
	connect(&m_pointsTimer, &QTimer::timeout, this, &BotEngine::pointsTick);
	m_pointsTimer.start();
	m_presenceTimer.setInterval(15000);
	connect(&m_presenceTimer, &QTimer::timeout, this, &BotEngine::refreshPresence);
	m_presenceTimer.start();
	m_questSaveTimer.setInterval(10000);
	connect(&m_questSaveTimer, &QTimer::timeout, this, [this]() { m_quests.flush(); });
	m_questSaveTimer.start();
	m_adventureTimer.setInterval(1000);
	connect(&m_adventureTimer, &QTimer::timeout, this, [this]() {
		adventureTick();
		bossTick();
	});
	m_adventureTimer.start();
}

QString BotEngine::keyFor(ChatPlatform platform, const QString &user)
{
	const QString name = user.trimmed().toLower();
	switch (platform) {
	case ChatPlatform::Twitch:
		/* Plain names: same keys as the Python bot's points.json. */
		return name;
	case ChatPlatform::Kick:
		return QStringLiteral("kick:") + name;
	case ChatPlatform::YouTube:
		return QStringLiteral("yt:") + name;
	}
	return name;
}

void BotEngine::setAudioDir(const QString &dir)
{
	m_audioDir = dir;
	reloadClips();
}

int BotEngine::reloadClips()
{
	m_clips.clear();
	if (!QDir(m_audioDir).exists())
		QDir().mkpath(m_audioDir);
	SoundLibrary::Defaults defaults;
	defaults.looseGroupName = t("Texuguito.Groups.LooseName");
	for (const QHash<QString, QString> *texts : {&m_cmdEn, &m_cmdPt}) {
		const QString name = texts->value(QStringLiteral("Texuguito.Groups.LooseName"));
		if (!name.isEmpty() && name != defaults.looseGroupName)
			defaults.otherLooseNames.append(name);
	}
	m_library.sync(m_audioDir, defaults);
	for (const SoundGroup &g : m_library.groups()) {
		for (const QString &name : g.sounds)
			m_clips[name] = AudioClip{
				name, g.price,
				QStringLiteral("/audios/") +
					QString::fromLatin1(QUrl::toPercentEncoding(m_library.file(name), "/")),
				g.id, g.enabled};
	}
	return static_cast<int>(m_clips.size());
}

QString BotEngine::addClip(const QByteArray &data, const QString &ext, const QString &name, int cost,
			   const QString &invoked)
{
	const auto tr = [&](const char *key) {
		return invoked.isEmpty() ? t(key) : tCmd(invoked, key);
	};
	if (cost < 0 || cost > kMaxClipCost)
		return tr("Texuguito.Bot.AddAudioBadPrice").arg(kMaxClipCost);
	const QString clean = SoundFetch::clipName(name);
	if (clean.isEmpty())
		return tr("Texuguito.Bot.AddAudioNoName");
	if (m_clips.count(clean))
		return tr("Texuguito.Bot.AddAudioExists").arg(clean);
	return addClipToGroup(data, ext, name, m_library.groupForPrice(cost, t("Texuguito.Groups.PriceName").arg(cost)),
			      invoked);
}

QString BotEngine::addClipToGroup(const QByteArray &data, const QString &ext, const QString &name,
				  const QString &groupId, const QString &invoked)
{
	const auto tr = [&](const char *key) {
		return invoked.isEmpty() ? t(key) : tCmd(invoked, key);
	};
	const QString clean = SoundFetch::clipName(name);
	if (clean.isEmpty())
		return tr("Texuguito.Bot.AddAudioNoName");
	if (!m_library.group(groupId))
		return tr("Texuguito.Groups.NoGroup");
	if (data.size() > SoundFetch::kMaxAudioBytes)
		return tr("Texuguito.Bot.AddAudioTooBig").arg(SoundFetch::kMaxAudioBytes / (1024 * 1024));
	if (SoundFetch::audioType(data.left(16)) != ext)
		return tr("Texuguito.Bot.AddAudioNoSound");
	if (m_clips.count(clean))
		return tr("Texuguito.Bot.AddAudioExists").arg(clean);
	const QString fileName = clean + QLatin1Char('.') + ext;
	QSaveFile file(QDir(m_audioDir).filePath(fileName));
	if (!QDir().mkpath(m_audioDir) || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
	    !file.commit())
		return tr("Texuguito.Bot.AddAudioSaveFailed");
	m_library.added(clean, fileName, groupId);
	reloadClips();
	return QString();
}

QString BotEngine::renameClip(const QString &name, const QString &newName)
{
	const QString clean = SoundFetch::clipName(newName);
	const QString from = m_library.file(name);
	if (from.isEmpty())
		return t("Texuguito.Bot.AudioNotFound").arg(name);
	if (clean.isEmpty())
		return t("Texuguito.Bot.AddAudioNoName");
	if (clean == name)
		return QString();
	if (m_clips.count(clean))
		return t("Texuguito.Bot.AddAudioExists").arg(clean);
	/* Same folder and type, new name. */
	const QFileInfo info(QDir(m_audioDir).filePath(from));
	const QString relative =
		QFileInfo(from).path() == QLatin1String(".")
			? clean + QLatin1Char('.') + info.suffix()
			: QFileInfo(from).path() + QLatin1Char('/') + clean + QLatin1Char('.') + info.suffix();
	if (!QFile::rename(info.filePath(), QDir(m_audioDir).filePath(relative)))
		return t("Texuguito.BotPanel.MoveFailed");
	m_library.renamed(name, clean, relative);
	reloadClips();
	return QString();
}

QString BotEngine::removeClip(const QString &name)
{
	const QString file = m_library.file(name);
	if (file.isEmpty())
		return t("Texuguito.Bot.AudioNotFound").arg(name);
	if (!QFile::remove(QDir(m_audioDir).filePath(file)))
		return t("Texuguito.BotPanel.DeleteFailed");
	m_library.removed(name);
	reloadClips();
	return QString();
}

QString BotEngine::soundFetchError(const SoundFetch::Result &result, const QString &invoked) const
{
	const auto tr = [&](const char *key) {
		return invoked.isEmpty() ? t(key) : tCmd(invoked, key);
	};
	switch (result.error) {
	case SoundFetch::Error::None:
		return QString();
	case SoundFetch::Error::BadLink:
		return tr("Texuguito.Bot.AddAudioBadLink");
	case SoundFetch::Error::Blocked:
		return tr("Texuguito.Bot.AddAudioBlocked").arg(result.detail);
	case SoundFetch::Error::NoAudio:
		return tr("Texuguito.Bot.AddAudioNoSound");
	case SoundFetch::Error::TooBig:
		return tr("Texuguito.Bot.AddAudioTooBig").arg(SoundFetch::kMaxAudioBytes / (1024 * 1024));
	case SoundFetch::Error::Network:
		break;
	}
	return tr("Texuguito.Bot.AddAudioDownloadFailed");
}

QString BotEngine::displayName(const QString &key)
{
	const Viewer &v = m_viewers.getOrCreate(key);
	if (!v.exibicao.isEmpty())
		return v.exibicao;
	const qsizetype colon = key.indexOf(QLatin1Char(':'));
	return colon < 0 ? key : key.mid(colon + 1);
}

void BotEngine::say(ChatPlatform platform, const QString &text)
{
	if (!text.isEmpty())
		emit reply(platform, text);
}

QJsonObject BotEngine::viewerPayload(const QString &key)
{
	const Viewer &v = m_viewers.getOrCreate(key);
	Status status = m_status.value(key);
	qint64 away = m_quests.awayFor(key);
	/* !dança or a gift from the same name on another platform animates this avatar. */
	for (const QString &twin : presentTwins(baseName(key))) {
		away = std::max(away, m_quests.awayFor(twin));
		status.dancingUntil = std::max(status.dancingUntil, m_status.value(twin).dancingUntil);
		status.cheerUntil = std::max(status.cheerUntil, m_status.value(twin).cheerUntil);
		if (m_status.value(twin).questUntil > status.questUntil) {
			status.questUntil = m_status.value(twin).questUntil;
			status.questLabel = m_status.value(twin).questLabel;
		}
	}
	const qint64 now = m_clock.elapsed();
	const auto remaining = [now](qint64 until) {
		return std::max<qint64>(0, until - now) / 1000.0;
	};
	const auto optional = [](const QString &s) {
		return s.isEmpty() ? QJsonValue() : QJsonValue(s);
	};
	static const char *const platforms[] = {"twitch", "youtube", "kick"};
	return QJsonObject{{QStringLiteral("username"), key},
			   {QStringLiteral("nick"), v.nick.isEmpty() ? displayName(key) : v.nick},
			   {QStringLiteral("cor"), v.cor},
			   {QStringLiteral("chapeu"), optional(v.chapeu)},
			   {QStringLiteral("acessorio"), optional(v.acessorio)},
			   {QStringLiteral("is_mod"), status.isMod},
			   {QStringLiteral("is_sub"), status.isSub},
			   /* The parade's sub effect grows with the tier. */
			   {QStringLiteral("sub_tier"), status.subTier},
			   {QStringLiteral("is_broadcaster"), status.isBroadcaster || status.streamer},
			   {QStringLiteral("platform"), QLatin1String(platforms[static_cast<int>(status.platform)])},
			   /* Seconds left, not booleans: the overlay turns them into
			    * its own deadlines and checks them every frame. */
			   {QStringLiteral("dance_remaining"), remaining(status.dancingUntil)},
			   {QStringLiteral("cheer_remaining"), remaining(status.cheerUntil)},
			   {QStringLiteral("quest_remaining"), remaining(status.questUntil)},
			   {QStringLiteral("quest_label"), status.questLabel},
			   /* On an adventure: the avatar walks off and comes back then. */
			   {QStringLiteral("adventure_remaining"), static_cast<double>(away) / 1000.0},
			   /* The avatar's quest level, when the panel shows it. */
			   {QStringLiteral("level"), m_quests.enabled() && m_quests.showLevel()
							     ? QJsonValue(m_quests.level(key))
							     : QJsonValue()}};
}

QString BotEngine::baseName(const QString &key)
{
	return key.mid(key.indexOf(QLatin1Char(':')) + 1);
}

QStringList BotEngine::presentTwins(const QString &base) const
{
	QList<std::pair<qint64, QString>> found;
	for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
		if (it->present && baseName(it.key()) == base)
			found.append({it->presentSince, it.key()});
	}
	std::sort(found.begin(), found.end());
	QStringList keys;
	for (const auto &f : found)
		keys.append(f.second);
	return keys;
}

QStringList BotEngine::lookKeys(const QString &key) const
{
	/* The parade draws one avatar per name, so a look command from any
	 * platform changes every present twin: the drawn one included. */
	QStringList keys = presentTwins(baseName(key));
	if (!keys.contains(key))
		keys.append(key);
	return keys;
}

void BotEngine::emitAvatar(const QString &type, const QString &key)
{
	emit overlayMessage(
		QJsonObject{{QStringLiteral("type"), type},
			    {QStringLiteral("username"), key},
			    {QStringLiteral("viewer"),
			     type == QLatin1String("left") ? QJsonValue() : QJsonValue(viewerPayload(key))}});
}

void BotEngine::syncAvatar(const QString &key)
{
	const QString base = baseName(key);
	const QStringList twins = presentTwins(base);
	const QString wanted = twins.isEmpty() ? QString() : twins.first();
	QString drawn;
	for (const QString &k : std::as_const(m_drawn)) {
		if (baseName(k) == base) {
			drawn = k;
			break;
		}
	}
	if (drawn == wanted) {
		if (!wanted.isEmpty())
			emitAvatar(QStringLiteral("updated"), wanted);
		return;
	}
	if (!drawn.isEmpty()) {
		m_drawn.remove(drawn);
		emitAvatar(QStringLiteral("left"), drawn);
	}
	if (!wanted.isEmpty()) {
		m_drawn.insert(wanted);
		emitAvatar(QStringLiteral("joined"), wanted);
	}
}

QJsonObject BotEngine::snapshot()
{
	QJsonArray present;
	for (const QString &key : std::as_const(m_drawn))
		present.append(viewerPayload(key));
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")},
			   {QStringLiteral("viewers"), present},
			   {QStringLiteral("boss"), bossJson()}};
}

void BotEngine::refreshPresence()
{
	const qint64 now = m_clock.elapsed();
	for (auto it = m_status.begin(); it != m_status.end(); ++it) {
		Status &s = it.value();
		const bool present = s.streamer || s.isBroadcaster || s.inChatters ||
				     (s.lastSeen > 0 && now - s.lastSeen < kPresenceMinutes * 60000LL);
		if (present == s.present)
			continue;
		s.present = present;
		if (present)
			s.presentSince = now;
		syncAvatar(it.key());
	}
}

void BotEngine::setTwitchChatters(const QSet<QString> &logins)
{
	QSet<QString> keys;
	for (const QString &login : logins)
		keys.insert(keyFor(ChatPlatform::Twitch, login));
	for (const QString &key : keys) {
		if (!m_status.contains(key)) {
			m_viewers.getOrCreate(key);
			m_status[key].platform = ChatPlatform::Twitch;
		}
	}
	for (auto it = m_status.begin(); it != m_status.end(); ++it) {
		if (it->platform == ChatPlatform::Twitch)
			it->inChatters = keys.contains(it.key());
	}
	refreshPresence();
}

void BotEngine::setStreamerChannels(const QList<QPair<ChatPlatform, QString>> &channels)
{
	QSet<QString> keys;
	for (const auto &[platform, name] : channels) {
		if (name.trimmed().isEmpty())
			continue;
		const QString key = keyFor(platform, name);
		keys.insert(key);
		if (!m_status.contains(key)) {
			m_viewers.getOrCreate(key, name.trimmed());
			m_status[key].platform = platform;
		}
	}
	for (auto it = m_status.begin(); it != m_status.end(); ++it)
		it->streamer = keys.contains(it.key());
	refreshPresence();
}

void BotEngine::pointsTick()
{
	/* A point per full minute in chat: present on this tick and the last. */
	QSet<QString> current;
	for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
		if (it->present)
			current.insert(it.key());
	}
	const QSet<QString> stayed = current & m_lastTickPresent;
	m_points.addMany(stayed, 1);
	m_lastTickPresent = current;
	for (const QString &key : stayed)
		questProgress(key, m_status.value(key).platform, Quests::Kind::Minutes);
}

void BotEngine::handleCheer(ChatPlatform platform, const QString &user)
{
	const QString key = keyFor(platform, user);
	m_viewers.getOrCreate(key, user);
	Status &s = m_status[key];
	s.platform = platform;
	s.cheerUntil = m_clock.elapsed() + kCheerMs;
	syncAvatar(key);
	questProgress(key, platform, Quests::Kind::Support);
	bossHit(key, BossFight::Hit::Support);
}

void BotEngine::handleQuestEvent(ChatPlatform platform, const QString &user, Quests::Kind kind)
{
	if (user.trimmed().isEmpty())
		return;
	const QString key = keyFor(platform, user);
	m_viewers.getOrCreate(key, user);
	if (!m_status.contains(key))
		m_status[key].platform = platform;
	questProgress(key, platform, kind);
	if (kind == Quests::Kind::Follow || kind == Quests::Kind::Sub)
		bossHit(key, kind == Quests::Kind::Sub ? BossFight::Hit::Sub : BossFight::Hit::Follow);
}

void BotEngine::handleMessage(const BotMessage &msg)
{
	if (msg.user.isEmpty())
		return;
	const QString key = keyFor(msg.platform, msg.user);
	m_viewers.getOrCreate(key, msg.user);
	Status &s = m_status[key];
	s.platform = msg.platform;
	s.isMod = msg.isMod;
	s.isSub = msg.isSub;
	s.subTier = msg.isSub ? std::clamp(msg.subTier, 1, 3) : 0;
	s.isBroadcaster = msg.isBroadcaster;
	s.lastSeen = std::max<qint64>(1, m_clock.elapsed());
	if (!s.present)
		s.presentSince = s.lastSeen;
	s.present = true;
	syncAvatar(key);

	const auto [name, args] = BotText::parseInvocation(msg.text, msg.isReply);
	if (name.isEmpty()) {
		/* A chat line, not a command: a step of the "messages" quests. */
		if (!msg.text.trimmed().isEmpty()) {
			questProgress(key, msg.platform, Quests::Kind::Messages);
			bossHit(key, BossFight::Hit::Message);
		}
		return;
	}

	if (s.isBroadcaster || s.streamer) {
		const QString text = msg.text.simplified().toLower();
		const qint64 now = m_clock.elapsed();
		StreamerCommand &last = m_lastStreamerCommand;
		if (last.at >= 0 && now - last.at < kSameCommandMs && last.text == text &&
		    last.platform != msg.platform)
			return;
		last = {text, msg.platform, now};
	}

	/* Chat-made commands first, but they can never shadow a built-in one. */
	if (!reservedNames().contains(name) && m_custom.contains(name)) {
		say(msg.platform, handleCustomCommand(msg, name, args));
		return;
	}
	if (const Command *cmd = findCommand(name))
		cmd->handler(msg, key, args);
}

const BotEngine::Command *BotEngine::findCommand(const QString &name) const
{
	for (const Command &c : m_commands) {
		if (c.name == name || c.aliases.contains(name))
			return &c;
	}
	return nullptr;
}

QStringList BotEngine::commandNames() const
{
	return reservedNames().values();
}

QSet<QString> BotEngine::reservedNames() const
{
	QSet<QString> names;
	for (const Command &c : m_commands) {
		names.insert(c.name);
		for (const QString &a : c.aliases)
			names.insert(a);
	}
	return names;
}

QString BotEngine::handleCustomCommand(const BotMessage &msg, const QString &name, const QStringList &args)
{
	QString response = m_custom.get(name);
	response.replace(QStringLiteral("{user}"), msg.user);
	response.replace(QStringLiteral("{usuario}"), msg.user);
	response.replace(QStringLiteral("{args}"), args.join(QLatin1Char(' ')));
	return response;
}

QString BotEngine::setCustomCommand(const QString &rawName, const QString &response)
{
	QString name = rawName.trimmed();
	while (name.startsWith(QLatin1Char('!')))
		name.remove(0, 1);
	name = name.toLower();
	static const QRegularExpression valid(QStringLiteral("^[\\w-]+$"),
					      QRegularExpression::UseUnicodePropertiesOption);
	if (name.isEmpty())
		return t("Texuguito.Bot.CommandNameMissing");
	if (name.size() > kCustomNameMax)
		return t("Texuguito.Bot.CommandNameTooLong").arg(kCustomNameMax);
	if (!valid.match(name).hasMatch())
		return t("Texuguito.Bot.CommandNameInvalid");
	if (reservedNames().contains(name))
		return t("Texuguito.Bot.CommandReserved").arg(name);
	const QString reply = response.trimmed();
	if (reply.isEmpty())
		return t("Texuguito.BotPanel.EmptyReply");
	if (reply.size() > kCustomResponseMax)
		return t("Texuguito.Bot.ReplyTooLong").arg(kCustomResponseMax);
	m_custom.set(name, reply);
	return QString();
}

QString BotEngine::handleComando(const BotMessage &msg, const QStringList &args)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	const auto tr = [&](const char *key) {
		return tCmd(invoked, key);
	};
	const QString action = args.isEmpty() ? QString() : args[0].toLower();
	const bool priv = privileged(msg);

	if (kList.contains(action) || (action.isEmpty() && !priv)) {
		const QStringList names = m_custom.names();
		if (names.isEmpty())
			return tr("Texuguito.Bot.NoCustomCommands");
		QStringList bang;
		for (const QString &n : names)
			bang.append(QLatin1Char('!') + n);
		return BotText::truncate(tr("Texuguito.Bot.CustomCommands").arg(bang.join(QStringLiteral(", "))));
	}
	if (!priv)
		return QString();

	const auto normalize = [](QString raw) {
		while (raw.startsWith(QLatin1Char('!')))
			raw.remove(0, 1);
		return raw.toLower();
	};
	const auto nameError = [&](const QString &name) -> QString {
		static const QRegularExpression valid(QStringLiteral("^[\\w-]+$"),
						      QRegularExpression::UseUnicodePropertiesOption);
		if (name.isEmpty())
			return tr("Texuguito.Bot.CommandNameMissing");
		if (name.size() > kCustomNameMax)
			return tr("Texuguito.Bot.CommandNameTooLong").arg(kCustomNameMax);
		if (!valid.match(name).hasMatch())
			return tr("Texuguito.Bot.CommandNameInvalid");
		return QString();
	};

	if (kAdd.contains(action) || kEdit.contains(action)) {
		if (args.size() < 3)
			return tr("Texuguito.Bot.CommandUsageAddEdit")
				.arg(action.isEmpty() ? QStringLiteral("add") : action);
		const QString name = normalize(args[1]);
		const QString error = nameError(name);
		if (!error.isEmpty())
			return error;
		if (reservedNames().contains(name))
			return tr("Texuguito.Bot.CommandReserved").arg(name);
		const bool exists = m_custom.contains(name);
		if (kAdd.contains(action) && exists)
			return tr("Texuguito.Bot.CommandExists").arg(name);
		if (kEdit.contains(action) && !exists)
			return tr("Texuguito.Bot.CommandMissing").arg(name);
		const QString response = args.mid(2).join(QLatin1Char(' '));
		if (response.size() > kCustomResponseMax)
			return tr("Texuguito.Bot.ReplyTooLong").arg(kCustomResponseMax);
		m_custom.set(name, response);
		return (exists ? tr("Texuguito.Bot.CommandUpdated") : tr("Texuguito.Bot.CommandCreated")).arg(name);
	}

	if (kRemove.contains(action)) {
		if (args.size() < 2)
			return tr("Texuguito.Bot.CommandUsageDel");
		const QString name = normalize(args[1]);
		if (!m_custom.remove(name))
			return tr("Texuguito.Bot.CommandNotFound").arg(name);
		return tr("Texuguito.Bot.CommandRemoved").arg(name);
	}

	return tr("Texuguito.Bot.CommandUsage");
}

void BotEngine::playTts(const BotMessage &msg, const QString &key, const QStringList &args)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	const auto tr = [&](const char *k) {
		return tCmd(invoked, k);
	};
	if (args.isEmpty()) {
		say(msg.platform, tr("Texuguito.Bot.TtsUsage"));
		return;
	}
	if (m_listeners <= 0) {
		say(msg.platform, tr("Texuguito.Bot.NoOverlay"));
		return;
	}
	if (!m_tts || !m_points.spend(key, kTtsCost)) {
		say(msg.platform, tr("Texuguito.Bot.TtsNoPoints").arg(kTtsCost));
		return;
	}
	/* The command picks the voice: !falar speaks Portuguese, !speak English,
	 * !tts the stream language. The spoken intro goes with the voice. */
	const bool englishVoice = invoked == QLatin1String("speak") || (invoked != QLatin1String("falar") && english());
	const QString lang = englishVoice ? QStringLiteral("en") : QStringLiteral("pt");
	const QString text =
		(englishVoice ? QStringLiteral("%1 sent the message: %2") : QStringLiteral("%1 enviou a mensagem: %2"))
			.arg(msg.user, args.join(QLatin1Char(' ')));
	const ChatPlatform platform = msg.platform;
	const QString user = msg.user;
	QPointer<BotEngine> self = this;
	m_tts(text, lang, [self, platform, user, key, invoked](QByteArray mp3, QString error) {
		if (!self)
			return;
		if (mp3.isEmpty()) {
			/* Nothing was played: give the points back. */
			self->m_points.add(key, kTtsCost);
			self->say(platform, self->tCmd(invoked, "Texuguito.Bot.TtsError"));
			Q_UNUSED(error);
			return;
		}
		const QString id = QUuid::createUuid().toString(QUuid::Id128);
		self->m_ttsClips.insert(id, mp3);
		self->m_ttsOrder.append(id);
		while (self->m_ttsOrder.size() > kTtsCacheSize)
			self->m_ttsClips.remove(self->m_ttsOrder.takeFirst());
		emit self->overlayMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio")},
						      {QStringLiteral("url"), QStringLiteral("/tts/") + id},
						      {QStringLiteral("volume"), self->m_volume}});
		self->say(platform, self->tCmd(invoked, "Texuguito.Bot.TtsSent").arg(user).arg(kTtsCost));
		self->questProgress(key, platform, Quests::Kind::Tts);
		self->questProgress(key, platform, Quests::Kind::Spend, kTtsCost);
	});
}

void BotEngine::sayPrivate(const BotMessage &msg, const QString &text)
{
	if (text.isEmpty())
		return;
	if (m_quests.whisper() && msg.platform == ChatPlatform::Twitch && !msg.userId.isEmpty())
		emit privateReply(msg.platform, msg.userId, text);
	else
		say(msg.platform, text);
}

QString BotEngine::questTitle(const QString &invoked, const QuestDef &quest) const
{
	if (!quest.title.isEmpty())
		return quest.title;
	const QByteArray key = (quest.goal == 1 ? QByteArrayLiteral("Texuguito.Parade.QuestOne.")
						: QByteArrayLiteral("Texuguito.Parade.QuestMany.")) +
			       Quests::kindId(quest.kind).toLatin1();
	const QString text = tCmd(invoked, key.constData());
	return quest.goal == 1 ? text : text.arg(quest.goal);
}

QString BotEngine::questsReply(const BotMessage &msg, const QString &key)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	if (!m_quests.enabled())
		return tCmd(invoked, "Texuguito.Bot.QuestsOff");
	QStringList drawn;
	QStringList bonus;
	for (const QuestEntry &e : m_quests.questsFor(key)) {
		if (Quests::isBonus(e.quest.kind)) {
			if (!e.done)
				bonus.append(questTitle(invoked, e.quest));
			continue;
		}
		const QString title = questTitle(invoked, e.quest);
		drawn.append(e.done ? QStringLiteral("✅ ") + title
			     : e.quest.goal > 1
				     ? QStringLiteral("⬜ %1 (%2/%3)").arg(title).arg(e.count).arg(e.quest.goal)
				     : QStringLiteral("⬜ ") + title);
	}
	if (drawn.isEmpty())
		drawn.append(tCmd(invoked, "Texuguito.Bot.QuestsEmpty"));
	if (!bonus.isEmpty())
		drawn.append(tCmd(invoked, "Texuguito.Bot.QuestsBonus").arg(bonus.join(QStringLiteral(", "))));
	const qint64 xp = m_quests.xp(key);
	const int level = Quests::levelFor(xp);
	return BotText::truncate(tCmd(invoked, "Texuguito.Bot.Quests")
					 .arg(msg.user)
					 .arg(level)
					 .arg(xp)
					 .arg(Quests::xpForLevel(level + 1))
					 .arg(drawn.join(QStringLiteral(" | "))));
}

void BotEngine::questProgress(const QString &key, ChatPlatform platform, Quests::Kind kind, qint64 amount)
{
	if (amount <= 0)
		return;
	const QList<QuestReward> rewards =
		m_quests.progress(key, kind, static_cast<int>(std::min<qint64>(amount, QuestBook::kMaxGoal)));
	if (rewards.isEmpty())
		return;
	QStringList labels;
	for (const QuestReward &r : rewards) {
		m_points.add(key, r.quest.points);
		labels.append(QStringLiteral("+%1 XP").arg(r.quest.xp));
		const QString name = displayName(key);
		if (m_quests.announce() == QuestBook::Announce::All)
			say(platform, tCmd(QString(), "Texuguito.Bot.QuestDone")
					      .arg(name, questTitle(QString(), r.quest))
					      .arg(r.quest.points)
					      .arg(r.quest.xp));
		if (r.levelUp && m_quests.announce() != QuestBook::Announce::None)
			say(platform, tCmd(QString(), "Texuguito.Bot.LevelUp").arg(name).arg(r.level));
	}
	for (const QString &line : earnItems(key, QString(), {}, labels))
		say(platform, line);
	Status &s = m_status[key];
	s.questUntil = m_clock.elapsed() + kQuestMs;
	s.questLabel = labels.join(QLatin1Char(' '));
	syncAvatar(key);
}

QStringList BotEngine::earnItems(const QString &key, const QString &invoked, QStringList fresh, QStringList &labels)
{
	/* Items go by name, like the avatar: ana on Twitch and on Kick share them. */
	fresh += m_items.onLevel(baseName(key), Quests::levelFor(m_quests.xp(key)));
	const bool en = tCmd(invoked, "Texuguito.Bot.Language") == QLatin1String("en");
	QStringList lines;
	for (const QString &id : std::as_const(fresh)) {
		const Items::Item *item = Items::find(id);
		if (!item)
			continue;
		const char *text = item->slot == Items::Slot::Hat ? "Texuguito.Bot.ItemUnlockedHat"
								  : "Texuguito.Bot.ItemUnlockedAccessory";
		lines.append(tCmd(invoked, text).arg(displayName(key), itemName(invoked, id), en ? item->english : id));
		labels.append(QStringLiteral("🎁 ") + itemName(invoked, id));
	}
	return lines;
}

QString BotEngine::itemName(const QString &invoked, const QString &id) const
{
	return tCmd(invoked, (QByteArrayLiteral("Texuguito.Parade.Item.") + id.toLatin1()).constData());
}

QString BotEngine::itemHow(const QString &invoked, const QString &id) const
{
	switch (m_items.rule(id)) {
	case Items::Rule::Level:
		return tCmd(invoked, "Texuguito.Bot.ItemHowLevel").arg(m_items.value(id));
	case Items::Rule::Adventure:
		return tCmd(invoked, "Texuguito.Bot.ItemHowAdventure").arg(m_items.value(id));
	case Items::Rule::Boss:
		return tCmd(invoked, "Texuguito.Bot.ItemHowBoss");
	case Items::Rule::Mvp:
		return tCmd(invoked, "Texuguito.Bot.ItemHowMvp");
	case Items::Rule::Shop:
		return tCmd(invoked, "Texuguito.Bot.ItemHowShop").arg(m_items.value(id));
	case Items::Rule::Off:
		break;
	}
	return tCmd(invoked, "Texuguito.Bot.ItemHowOff");
}

bool BotEngine::mayWear(const BotMessage &msg, const QString &key, const QString &id)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	/* A level reached before the items came also counts. */
	QStringList labels;
	for (const QString &line : earnItems(key, invoked, {}, labels))
		say(msg.platform, line);
	if (m_items.owns(baseName(key), id))
		return true;
	say(msg.platform,
	    tCmd(invoked, "Texuguito.Bot.ItemLocked").arg(msg.user, itemName(invoked, id), itemHow(invoked, id)));
	return false;
}

QString BotEngine::itemsReply(const BotMessage &msg, const QString &key)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	QStringList labels;
	for (const QString &line : earnItems(key, invoked, {}, labels))
		say(msg.platform, line);
	const bool en = tCmd(invoked, "Texuguito.Bot.Language") == QLatin1String("en");
	QStringList mine;
	QStringList next;
	int forSale = 0;
	for (const Items::Item &item : Items::catalog()) {
		const Items::Rule rule = m_items.rule(item.id);
		if (m_items.owns(baseName(key), item.id))
			mine.append(
				QStringLiteral("%1 (%2)").arg(itemName(invoked, item.id), en ? item.english : item.id));
		else if (rule == Items::Rule::Shop)
			/* The shop lists them itself, with the prices. */
			++forSale;
		else if (rule != Items::Rule::Off)
			next.append(
				QStringLiteral("%1: %2").arg(itemName(invoked, item.id), itemHow(invoked, item.id)));
	}
	QString text = mine.isEmpty()
			       ? tCmd(invoked, "Texuguito.Bot.ItemsNone").arg(msg.user)
			       : tCmd(invoked, "Texuguito.Bot.Items").arg(msg.user, mine.join(QStringLiteral(", ")));
	if (!next.isEmpty())
		text += tCmd(invoked, "Texuguito.Bot.ItemsNext").arg(next.join(QStringLiteral(", ")));
	if (forSale > 0)
		text += tCmd(invoked, "Texuguito.Bot.ItemsShop").arg(forSale);
	return BotText::truncate(text);
}

QString BotEngine::shopReply(const BotMessage &msg, const QString &key)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	const QStringList ids = m_items.forSale();
	if (ids.isEmpty())
		return tCmd(invoked, "Texuguito.Bot.ShopClosed");
	const bool en = tCmd(invoked, "Texuguito.Bot.Language") == QLatin1String("en");
	QStringList offers;
	for (const QString &id : ids) {
		const Items::Item *item = Items::find(id);
		const QString name =
			QStringLiteral("%1 (%2)").arg(itemName(invoked, id), en ? item->english : item->id);
		offers.append(m_items.owns(baseName(key), id)
				      ? name + QStringLiteral(" ✓")
				      : QStringLiteral("%1 %2 pts").arg(name).arg(m_items.value(id)));
	}
	return BotText::truncate(
		tCmd(invoked, "Texuguito.Bot.Shop").arg(offers.join(QStringLiteral(", "))).arg(m_points.get(key)));
}

void BotEngine::buyItem(const BotMessage &msg, const QString &key, const QString &raw)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	const QString id = Items::resolve(raw);
	if (id.isEmpty() || m_items.rule(id) != Items::Rule::Shop) {
		say(msg.platform, tCmd(invoked, "Texuguito.Bot.ShopUnknown").arg(msg.user));
		return;
	}
	const QString name = itemName(invoked, id);
	if (m_items.owns(baseName(key), id)) {
		say(msg.platform, tCmd(invoked, "Texuguito.Bot.ShopOwned").arg(msg.user, name));
		return;
	}
	const int price = m_items.value(id);
	if (!m_points.spend(key, price)) {
		say(msg.platform,
		    tCmd(invoked, "Texuguito.Bot.ShopNoPoints").arg(msg.user, name).arg(price).arg(m_points.get(key)));
		return;
	}
	m_items.unlock(baseName(key), id);
	const Items::Item *item = Items::find(id);
	const bool en = tCmd(invoked, "Texuguito.Bot.Language") == QLatin1String("en");
	const char *text = item->slot == Items::Slot::Hat ? "Texuguito.Bot.ShopBoughtHat"
							  : "Texuguito.Bot.ShopBoughtAccessory";
	/* Always told, like an item earned: the rest of the chat sees the shop works. */
	say(msg.platform, tCmd(invoked, text).arg(displayName(key), name, en ? item->english : id).arg(price));
	questProgress(key, msg.platform, Quests::Kind::Spend, price);
	/* Next to a quest just done, if spending finished one. */
	Status &s = m_status[key];
	const qint64 now = m_clock.elapsed();
	const QString label = QStringLiteral("🎁 ") + name;
	s.questLabel = s.questUntil > now ? s.questLabel + QLatin1Char(' ') + label : label;
	s.questUntil = now + kQuestMs;
	syncAvatar(key);
}

QString BotEngine::waitText(qint64 ms)
{
	const qint64 seconds = (ms + 999) / 1000;
	return seconds >= 60 ? QStringLiteral("%1 min").arg((seconds + 59) / 60) : QStringLiteral("%1 s").arg(seconds);
}

QString BotEngine::outcomeTitle(const QString &invoked, const AdventureOutcome &outcome) const
{
	if (!outcome.title.isEmpty())
		return outcome.title;
	/* The default prizes have their own text; a new one without a title
	 * gets a generic one. */
	static const QSet<QString> known{QStringLiteral("nothing"), QStringLiteral("coins"), QStringLiteral("treasure"),
					 QStringLiteral("legendary")};
	const QByteArray key = QByteArrayLiteral("Texuguito.Parade.AdventureOutcome.") +
			       (known.contains(outcome.id) ? outcome.id.toLatin1() : QByteArrayLiteral("custom"));
	return tCmd(invoked, key.constData());
}

void BotEngine::startAdventure(const BotMessage &msg, const QString &key)
{
	const QString invoked = BotText::parseInvocation(msg.text, msg.isReply).first;
	if (!m_quests.adventureEnabled()) {
		say(msg.platform, tCmd(invoked, "Texuguito.Bot.AdventureOff"));
		return;
	}
	/* One that just ended comes back first, so it can go again. */
	adventureTick();
	if (const qint64 left = m_quests.awayFor(key); left > 0) {
		say(msg.platform, tCmd(invoked, "Texuguito.Bot.AdventureAway").arg(msg.user, waitText(left)));
		return;
	}
	const int cost = m_quests.adventureCost();
	if (!m_points.spend(key, cost)) {
		say(msg.platform,
		    tCmd(invoked, "Texuguito.Bot.AdventureNoPoints").arg(msg.user).arg(cost).arg(m_points.get(key)));
		return;
	}
	/* Where and in which language to tell how it went. */
	m_quests.startAdventure(key, QStringLiteral("%1:%2").arg(static_cast<int>(msg.platform)).arg(invoked));
	syncAvatar(key);
	say(msg.platform,
	    tCmd(invoked, "Texuguito.Bot.AdventureStart").arg(msg.user).arg(m_quests.adventureMinutes()).arg(cost));
	questProgress(key, msg.platform, Quests::Kind::Spend, cost);
}

void BotEngine::adventureTick()
{
	for (const AdventureResult &r : m_quests.finishAdventures()) {
		const qsizetype colon = r.tag.indexOf(QLatin1Char(':'));
		const auto platform = static_cast<ChatPlatform>(std::clamp(r.tag.left(colon).toInt(), 0, 2));
		const QString invoked = colon < 0 ? QString() : r.tag.mid(colon + 1);
		m_points.add(r.key, r.outcome.points);
		const QString name = displayName(r.key);
		say(platform, tCmd(invoked, "Texuguito.Bot.AdventureBack")
				      .arg(name, outcomeTitle(invoked, r.outcome))
				      .arg(r.outcome.points)
				      .arg(r.outcome.xp));
		if (r.levelUp && m_quests.announce() != QuestBook::Announce::None)
			say(platform, tCmd(invoked, "Texuguito.Bot.LevelUp").arg(name).arg(r.level));
		if (!m_status.contains(r.key))
			m_status[r.key].platform = platform;
		m_status[r.key].questUntil = 0;
		/* Coming back may finish an adventure quest: one label for both. */
		questProgress(r.key, platform, Quests::Kind::Adventure);
		QStringList items;
		for (const QString &line : earnItems(r.key, invoked, m_items.onAdventure(baseName(r.key)), items))
			say(platform, line);
		Status &s = m_status[r.key];
		QString label = QStringLiteral("+%1 XP").arg(r.outcome.xp);
		if (r.outcome.points > 0)
			label.prepend(QStringLiteral("+%1 pts ").arg(r.outcome.points));
		if (s.questUntil > 0)
			label += QLatin1Char(' ') + s.questLabel;
		if (!items.isEmpty())
			label += QLatin1Char(' ') + items.join(QLatin1Char(' '));
		s.questUntil = m_clock.elapsed() + kQuestMs;
		s.questLabel = label;
		syncAvatar(r.key);
	}
}

QString BotEngine::bossName() const
{
	return m_boss.name().isEmpty() ? tCmd(QString(), "Texuguito.Parade.BossDefaultName") : m_boss.name();
}

QJsonObject BotEngine::bossJson() const
{
	return QJsonObject{{QStringLiteral("active"), m_boss.active()},
			   {QStringLiteral("name"), bossName()},
			   {QStringLiteral("hp"), m_boss.hp()},
			   {QStringLiteral("max_hp"), m_boss.maxHp()},
			   {QStringLiteral("remaining"), static_cast<double>(m_boss.remainingMs()) / 1000.0}};
}

void BotEngine::sayEverywhere(const QString &text, std::optional<ChatPlatform> also)
{
	QSet<int> platforms;
	if (also)
		platforms.insert(static_cast<int>(*also));
	for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
		if (it->present && !it->streamer)
			platforms.insert(static_cast<int>(it->platform));
	}
	for (const ChatPlatform p : {ChatPlatform::Twitch, ChatPlatform::YouTube, ChatPlatform::Kick}) {
		if (platforms.contains(static_cast<int>(p)))
			say(p, text);
	}
}

bool BotEngine::startBoss()
{
	int present = 0;
	for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
		if (it->present && !it->streamer && !it->isBroadcaster)
			++present;
	}
	if (!m_boss.start(present))
		return false;
	emit overlayMessage(
		QJsonObject{{QStringLiteral("type"), QStringLiteral("boss")}, {QStringLiteral("boss"), bossJson()}});
	sayEverywhere(
		tCmd(QString(), "Texuguito.Bot.BossStart").arg(bossName()).arg(m_boss.maxHp()).arg(m_boss.minutes()));
	return true;
}

void BotEngine::bossHit(const QString &key, BossFight::Hit hit)
{
	const int damage = m_boss.hit(key, hit);
	if (damage <= 0)
		return;
	QJsonObject boss = bossJson();
	boss.insert(QStringLiteral("hit"), QJsonObject{{QStringLiteral("username"), key},
						       {QStringLiteral("nick"), displayName(key)},
						       {QStringLiteral("damage"), damage}});
	emit overlayMessage(
		QJsonObject{{QStringLiteral("type"), QStringLiteral("boss")}, {QStringLiteral("boss"), boss}});
	/* The last hit ends it at once. */
	if (m_boss.hp() <= 0)
		bossTick();
}

void BotEngine::bossTick()
{
	if (const std::optional<BossFight::Result> result = m_boss.finish())
		endBoss(*result);
	else if (m_boss.autoDue(m_live))
		startBoss();
}

void BotEngine::endBoss(const BossFight::Result &result)
{
	QJsonObject boss = bossJson();
	boss.insert(QStringLiteral("result"), result.defeated ? QStringLiteral("defeated") : QStringLiteral("escaped"));
	emit overlayMessage(
		QJsonObject{{QStringLiteral("type"), QStringLiteral("boss")}, {QStringLiteral("boss"), boss}});
	if (!result.defeated) {
		sayEverywhere(tCmd(QString(), "Texuguito.Bot.BossEscaped").arg(bossName()).arg(result.hpLeft));
		return;
	}
	const int points = m_boss.points();
	const int xp = m_boss.xp();
	QStringList levelUps;
	QList<QPair<ChatPlatform, QString>> unlocked; /* told after the result, in each one's chat */
	for (qsizetype i = 0; i < result.damage.size(); ++i) {
		const QString &key = result.damage[i].first;
		const int bonus = i == 0 ? m_boss.mvpPoints() : 0;
		m_points.add(key, points + bonus);
		const QuestBook::XpGain gain = m_quests.addXp(key, xp);
		if (gain.levelUp)
			levelUps.append(tCmd(QString(), "Texuguito.Bot.LevelUp").arg(displayName(key)).arg(gain.level));
		if (!m_status.contains(key))
			m_status[key].platform = ChatPlatform::Twitch;
		const ChatPlatform platform = m_status.value(key).platform;
		m_status[key].questUntil = 0;
		questProgress(key, platform, Quests::Kind::Boss);
		QStringList items;
		for (const QString &line : earnItems(key, QString(), m_items.onBoss(baseName(key), i == 0), items))
			unlocked.append({platform, line});
		Status &s = m_status[key];
		QString label = QStringLiteral("+%1 pts +%2 XP").arg(points + bonus).arg(xp);
		if (s.questUntil > 0)
			label += QLatin1Char(' ') + s.questLabel;
		if (!items.isEmpty())
			label += QLatin1Char(' ') + items.join(QLatin1Char(' '));
		s.questUntil = m_clock.elapsed() + kQuestMs;
		s.questLabel = label;
		syncAvatar(key);
	}
	m_quests.flush();
	const QPair<QString, int> mvp = result.damage.value(0);
	sayEverywhere(BotText::truncate(tCmd(QString(), "Texuguito.Bot.BossDefeated")
						.arg(bossName())
						.arg(result.damage.size())
						.arg(points)
						.arg(xp)
						.arg(mvp.first.isEmpty() ? QStringLiteral("-") : displayName(mvp.first))
						.arg(mvp.second)
						.arg(m_boss.mvpPoints())));
	if (m_quests.announce() != QuestBook::Announce::None)
		for (const QString &line : std::as_const(levelUps))
			sayEverywhere(line);
	for (const auto &[platform, line] : std::as_const(unlocked))
		say(platform, line);
}

void BotEngine::registerCommands()
{
	const auto updated = [this](const QString &key) {
		syncAvatar(key);
	};
	/* A viewer changed their own look: also a step of the "look" quests. */
	const auto restyled = [this](const BotMessage &m, const QString &key) {
		syncAvatar(key);
		questProgress(key, m.platform, Quests::Kind::Look);
	};

	m_commands = {
		{QStringLiteral("cor"),
		 {QStringLiteral("color"), QStringLiteral("colour")},
		 [this, restyled](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.ColorUsage").arg(m.user));
				 return;
			 }
			 const auto cor = BotData::validateColor(args.join(QLatin1Char(' ')));
			 if (!cor) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.ColorInvalid").arg(m.user));
				 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setColor(k, *cor);
			 restyled(m, key);
		 }},
		{QStringLiteral("resetcor"),
		 {QStringLiteral("resetcolor"), QStringLiteral("resetcolour")},
		 [this, restyled](const BotMessage &m, const QString &key, const QStringList &) {
			 for (const QString &k : lookKeys(key))
				 m_viewers.resetColor(k);
			 restyled(m, key);
		 }},
		{QStringLiteral("chapeu"),
		 {QStringLiteral("hat")},
		 [this, restyled](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 const bool listEnglish = commandEnglish(invoked).value_or(english());
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.HatUsage").arg(m.user));
				 return;
			 }
			 const QString raw = args.join(QLatin1Char(' '));
			 auto [ok, value] = BotData::validateHat(raw);
			 if (!ok) {
				 value = Items::resolve(raw, Items::Slot::Hat);
				 if (value.isEmpty()) {
					 say(m.platform,
					     tCmd(invoked, "Texuguito.Bot.HatInvalid")
						     .arg(m.user,
							  BotData::hatNames(listEnglish).join(QStringLiteral(", "))));
					 return;
				 }
				 if (!mayWear(m, key, value))
					 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setHat(k, value);
			 restyled(m, key);
		 }},
		{QStringLiteral("acessorio"),
		 {QStringLiteral("accessory")},
		 [this, restyled](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 const bool listEnglish = commandEnglish(invoked).value_or(english());
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AccessoryUsage").arg(m.user));
				 return;
			 }
			 const QString raw = args.join(QLatin1Char(' '));
			 auto [ok, value] = BotData::validateAccessory(raw);
			 if (!ok) {
				 value = Items::resolve(raw, Items::Slot::Accessory);
				 if (value.isEmpty()) {
					 say(m.platform, tCmd(invoked, "Texuguito.Bot.AccessoryInvalid")
								 .arg(m.user, BotData::accessoryNames(listEnglish)
										      .join(QStringLiteral(", "))));
					 return;
				 }
				 if (!mayWear(m, key, value))
					 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setAccessory(k, value);
			 restyled(m, key);
		 }},
		{QStringLiteral("apelido"),
		 {QStringLiteral("nick"), QStringLiteral("nickname")},
		 [this, restyled](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.NickUsage").arg(m.user));
				 return;
			 }
			 const QString nick = BotData::validateNick(args.join(QLatin1Char(' ')));
			 if (nick.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.NickInvalid").arg(m.user));
				 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setNick(k, nick);
			 restyled(m, key);
		 }},
		{QStringLiteral("dança"),
		 {QStringLiteral("danca"), QStringLiteral("dance")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &) {
			 m_status[key].dancingUntil = m_clock.elapsed() + kDanceMs;
			 updated(key);
			 questProgress(key, m.platform, Quests::Kind::Dance);
		 }},
		{QStringLiteral("avatarmod"),
		 {QStringLiteral("setavatar")},
		 [this, updated](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!privileged(m))
				 return;
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (args.size() < 2) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AvatarModUsage").arg(m.user));
				 return;
			 }
			 const auto cor = BotData::validateColor(args.mid(1).join(QLatin1Char(' ')));
			 if (!cor) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AvatarModInvalid").arg(m.user));
				 return;
			 }
			 QString target = args[0];
			 while (target.startsWith(QLatin1Char('@')))
				 target.remove(0, 1);
			 const QString targetKey = keyFor(m.platform, target);
			 for (const QString &k : lookKeys(targetKey))
				 m_viewers.setColor(k, *cor);
			 updated(targetKey);
		 }},
		{QStringLiteral("comandos"),
		 {QStringLiteral("ajuda"), QStringLiteral("help"), QStringLiteral("commands")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 /* The wording and the page follow the command's language. */
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 const bool englishPage = commandEnglish(invoked).value_or(english());
			 say(m.platform,
			     tCmd(invoked, "Texuguito.Bot.Commands")
				     .arg(m.user, QLatin1String(englishPage ? kCommandsUrlEn : kCommandsUrlPt)));
		 }},
		{QStringLiteral("comando"),
		 {QStringLiteral("cmd"), QStringLiteral("command")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 say(m.platform, handleComando(m, args));
		 }},
		{QStringLiteral("ping"),
		 {},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.Pong").arg(m.user));
		 }},
		{QStringLiteral("pontos"),
		 {QStringLiteral("pts"), QStringLiteral("points")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.Points").arg(m.user).arg(m_points.get(key)));
		 }},
		{QStringLiteral("darpontos"),
		 {QStringLiteral("dar"), QStringLiteral("addpontos"), QStringLiteral("addpoints"),
		  QStringLiteral("give"), QStringLiteral("givepoints")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!privileged(m))
				 return;
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 bool ok = false;
			 const qint64 amount = args.size() >= 2 ? args[1].toLongLong(&ok) : 0;
			 if (!ok) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.GivePointsUsage"));
				 return;
			 }
			 QString target = args[0].toLower();
			 while (target.startsWith(QLatin1Char('@')))
				 target.remove(0, 1);
			 const QString targetKey = keyFor(m.platform, target);
			 m_points.add(targetKey, amount);
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.PointsGiven")
						 .arg(amount)
						 .arg(target)
						 .arg(m_points.get(targetKey)));
		 }},
		{QStringLiteral("addaudio"),
		 {QStringLiteral("adicionaraudio"), QStringLiteral("addsom"), QStringLiteral("addsound")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!privileged(m))
				 return;
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 bool priceOk = false;
			 const int cost = args.size() >= 2 ? args[1].toInt(&priceOk) : 0;
			 if (!priceOk || !m_soundFetch) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AddAudioUsage"));
				 return;
			 }
			 const QString wanted = args.size() >= 3 ? args.mid(2).join(QLatin1Char('-')) : QString();
			 const ChatPlatform platform = m.platform;
			 QPointer<BotEngine> self = this;
			 m_soundFetch(args[0], [self, platform, cost, wanted, invoked](const SoundFetch::Result &r) {
				 if (!self)
					 return;
				 if (r.error != SoundFetch::Error::None) {
					 self->say(platform, self->soundFetchError(r, invoked));
					 return;
				 }
				 const QString name = SoundFetch::clipName(wanted.isEmpty() ? r.name : wanted);
				 const QString error = self->addClip(r.data, r.ext, name, cost, invoked);
				 self->say(platform,
					   error.isEmpty()
						   ? self->tCmd(invoked, "Texuguito.Bot.AudioAdded").arg(name).arg(cost)
						   : error);
			 });
		 }},
		{QStringLiteral("tocar"),
		 {QStringLiteral("p"), QStringLiteral("play")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.PlayUsage"));
				 return;
			 }
			 const QString name = args.join(QLatin1Char(' ')).toLower();
			 const auto clip = m_clips.find(name);
			 if (clip == m_clips.end()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AudioNotFound").arg(name));
				 return;
			 }
			 const SoundGroup *group = m_library.group(clip->second.group);
			 if (!group || !group->enabled) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AudioOff").arg(name));
				 return;
			 }
			 /* Each group has its own wait: a sound of another group can
			  * play right after. */
			 const QElapsedTimer last = m_lastClip.value(group->id);
			 if (last.isValid()) {
				 const qint64 left = group->cooldown * 1000LL - last.elapsed();
				 if (left > 0) {
					 say(m.platform, tCmd(invoked, "Texuguito.Bot.Cooldown")
								 .arg(group->name)
								 .arg(left / 1000 + 1));
					 return;
				 }
			 }
			 if (m_listeners <= 0) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.NoOverlay"));
				 return;
			 }
			 if (!m_points.spend(key, clip->second.cost)) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.PlayNoPoints")
							 .arg(clip->second.name)
							 .arg(clip->second.cost));
				 return;
			 }
			 m_lastClip[group->id].start();
			 emit overlayMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio")},
							 {QStringLiteral("url"), clip->second.url},
							 {QStringLiteral("volume"), m_volume}});
			 say(m.platform,
			     tCmd(invoked, "Texuguito.Bot.Playing").arg(clip->second.name).arg(m_points.get(key)));
			 questProgress(key, m.platform, Quests::Kind::Sound);
			 questProgress(key, m.platform, Quests::Kind::Spend, clip->second.cost);
		 }},
		{QStringLiteral("falar"),
		 {QStringLiteral("tts"), QStringLiteral("speak")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 playTts(m, key, args);
		 }},
		{QStringLiteral("audios"),
		 {QStringLiteral("sons"), QStringLiteral("sounds"), QStringLiteral("audio")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 /* One block per group that is on, cheapest first; a name
			  * like "50 pts" already says the price. */
			 QList<const SoundGroup *> on;
			 for (const SoundGroup &g : m_library.groups())
				 if (g.enabled && !g.sounds.isEmpty())
					 on.append(&g);
			 if (on.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.NoAudios"));
				 return;
			 }
			 std::stable_sort(on.begin(), on.end(),
					  [](const SoundGroup *a, const SoundGroup *b) { return a->price < b->price; });
			 QStringList parts;
			 for (const SoundGroup *g : on) {
				 QStringList names = g->sounds;
				 names.sort();
				 const QString label =
					 g->name.contains(QString::number(g->price))
						 ? g->name
						 : QStringLiteral("%1 · %2 pts").arg(g->name).arg(g->price);
				 parts.append(QStringLiteral("[%1: %2]").arg(label, names.join(QStringLiteral(", "))));
			 }
			 say(m.platform,
			     BotText::truncate(
				     tCmd(invoked, "Texuguito.Bot.Sounds").arg(parts.join(QStringLiteral(" | ")))));
		 }},
		{QStringLiteral("parar"),
		 {QStringLiteral("stop")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 emit overlayMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio_stop")}});
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.Stopped"));
		 }},
		{QStringLiteral("recarregar"),
		 {QStringLiteral("reload")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 if (privileged(m)) {
				 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.Reloaded").arg(reloadClips()));
			 }
		 }},
		{QStringLiteral("status"),
		 {QStringLiteral("estado")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.Status").arg(m_clips.size()));
		 }},
		{QStringLiteral("sorteio"),
		 {QStringLiteral("raffle")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!m.isBroadcaster)
				 return;
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (m_raffle.active()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.RaffleRunning"));
				 return;
			 }
			 const qint64 prize = args.size() >= 2 ? args[0].toLongLong() : 0;
			 const int minutes = args.size() >= 2 ? args[1].toInt() : 0;
			 if (prize <= 0 || minutes <= 0) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.RaffleUsage"));
				 return;
			 }
			 m_raffle.start(prize);
			 say(m.platform, tCmd(invoked, "Texuguito.Bot.RaffleStarted").arg(prize).arg(minutes));
			 const ChatPlatform platform = m.platform;
			 QTimer::singleShot(minutes * 60000, this, [this, platform, invoked]() {
				 const auto [winner, won] = m_raffle.finish(m_points);
				 if (winner.isEmpty())
					 say(platform, tCmd(invoked, "Texuguito.Bot.RaffleEmpty"));
				 else
					 say(platform, tCmd(invoked, "Texuguito.Bot.RaffleWinner")
							       .arg(displayName(winner))
							       .arg(won));
			 });
		 }},
		{QStringLiteral("entrar"),
		 {QStringLiteral("join"), QStringLiteral("enter")},
		 /* Silent on purpose: a reply per participant would flood the chat. */
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 if (m_raffle.join(key))
				 questProgress(key, m.platform, Quests::Kind::Raffle);
		 }},
		{QStringLiteral("quests"),
		 {QStringLiteral("missoes"), QStringLiteral("missões"), QStringLiteral("quest"),
		  QStringLiteral("missions")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 sayPrivate(m, questsReply(m, key));
		 }},
		{QStringLiteral("aventura"),
		 {QStringLiteral("adventure")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 startAdventure(m, key);
		 }},
		{QStringLiteral("atacar"),
		 {QStringLiteral("attack")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 if (!m_boss.active()) {
				 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.BossNone").arg(m.user));
				 return;
			 }
			 /* Silent: the damage shows on the boss, not in the chat. */
			 bossHit(key, BossFight::Hit::Attack);
		 }},
		{QStringLiteral("chefao"),
		 {QStringLiteral("chefão"), QStringLiteral("boss")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (m_boss.active()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.BossStatus")
							 .arg(bossName())
							 .arg(m_boss.hp())
							 .arg(m_boss.maxHp())
							 .arg(waitText(m_boss.remainingMs())));
				 return;
			 }
			 if (!privileged(m)) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.BossNone").arg(m.user));
				 return;
			 }
			 if (!startBoss())
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.BossOff"));
		 }},
		{QStringLiteral("nivel"),
		 {QStringLiteral("nível"), QStringLiteral("level"), QStringLiteral("lvl"), QStringLiteral("xp")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 if (!m_quests.enabled()) {
				 sayPrivate(m, tCmd(invoked, "Texuguito.Bot.QuestsOff"));
				 return;
			 }
			 const qint64 xp = m_quests.xp(key);
			 const int level = Quests::levelFor(xp);
			 sayPrivate(m, tCmd(invoked, "Texuguito.Bot.Level")
					       .arg(m.user)
					       .arg(level)
					       .arg(xp)
					       .arg(Quests::xpForLevel(level + 1)));
		 }},
		{QStringLiteral("itens"),
		 {QStringLiteral("items"), QStringLiteral("inventario"), QStringLiteral("inventário"),
		  QStringLiteral("inventory")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 sayPrivate(m, itemsReply(m, key));
		 }},
		{QStringLiteral("loja"),
		 {QStringLiteral("shop"), QStringLiteral("store")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 /* !loja <item> buys it too. */
			 if (args.isEmpty())
				 sayPrivate(m, shopReply(m, key));
			 else
				 buyItem(m, key, args.join(QLatin1Char(' ')));
		 }},
		{QStringLiteral("comprar"),
		 {QStringLiteral("buy")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty())
				 sayPrivate(m, shopReply(m, key));
			 else
				 buyItem(m, key, args.join(QLatin1Char(' ')));
		 }},
	};
}
