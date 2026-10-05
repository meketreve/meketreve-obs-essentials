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
	QStringLiteral("join"),      QStringLiteral("addsound"),
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
	defaults.priceGroupName = [this](int price) {
		return t("Texuguito.Groups.PriceName").arg(price);
	};
	defaults.priceCooldown = [this](int price) {
		return m_cooldowns.value(price, defaultCooldownSeconds(price));
	};
	defaults.looseGroupName = t("Texuguito.Groups.LooseName");
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

void BotEngine::reloadData()
{
	m_viewers.load();
	m_points.load();
	m_custom.load();
	reloadClips();
	refreshPresence();
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
	/* !dança or a gift from the same name on another platform animates this avatar. */
	for (const QString &twin : presentTwins(baseName(key))) {
		status.dancingUntil = std::max(status.dancingUntil, m_status.value(twin).dancingUntil);
		status.cheerUntil = std::max(status.cheerUntil, m_status.value(twin).cheerUntil);
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
			   {QStringLiteral("is_broadcaster"), status.isBroadcaster || status.streamer},
			   {QStringLiteral("platform"), QLatin1String(platforms[static_cast<int>(status.platform)])},
			   /* Seconds left, not booleans: the overlay turns them into
			    * its own deadlines and checks them every frame. */
			   {QStringLiteral("dance_remaining"), remaining(status.dancingUntil)},
			   {QStringLiteral("cheer_remaining"), remaining(status.cheerUntil)}};
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
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")}, {QStringLiteral("viewers"), present}};
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
	m_points.addMany(current & m_lastTickPresent, 1);
	m_lastTickPresent = current;
}

void BotEngine::handleCheer(ChatPlatform platform, const QString &user)
{
	const QString key = keyFor(platform, user);
	m_viewers.getOrCreate(key, user);
	Status &s = m_status[key];
	s.platform = platform;
	s.cheerUntil = m_clock.elapsed() + kCheerMs;
	syncAvatar(key);
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
	s.isBroadcaster = msg.isBroadcaster;
	s.lastSeen = std::max<qint64>(1, m_clock.elapsed());
	if (!s.present)
		s.presentSince = s.lastSeen;
	s.present = true;
	syncAvatar(key);

	const auto [name, args] = BotText::parseInvocation(msg.text, msg.isReply);
	if (name.isEmpty())
		return;

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

int BotEngine::defaultCooldownSeconds(int cost)
{
	if (cost <= 20)
		return 10;
	if (cost <= 100)
		return 30;
	if (cost <= 200)
		return 60;
	return 120;
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
	});
}

void BotEngine::registerCommands()
{
	const auto updated = [this](const QString &key) {
		syncAvatar(key);
	};

	m_commands = {
		{QStringLiteral("cor"),
		 {QStringLiteral("color")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
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
			 updated(key);
		 }},
		{QStringLiteral("resetcor"),
		 {QStringLiteral("resetcolor")},
		 [this, updated](const BotMessage &, const QString &key, const QStringList &) {
			 for (const QString &k : lookKeys(key))
				 m_viewers.resetColor(k);
			 updated(key);
		 }},
		{QStringLiteral("chapeu"),
		 {QStringLiteral("hat")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 const bool listEnglish = commandEnglish(invoked).value_or(english());
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.HatUsage").arg(m.user));
				 return;
			 }
			 const auto [ok, value] = BotData::validateHat(args.join(QLatin1Char(' ')));
			 if (!ok) {
				 say(m.platform,
				     tCmd(invoked, "Texuguito.Bot.HatInvalid")
					     .arg(m.user, BotData::hatNames(listEnglish).join(QStringLiteral(", "))));
				 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setHat(k, value);
			 updated(key);
		 }},
		{QStringLiteral("acessorio"),
		 {QStringLiteral("accessory")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 const QString invoked = BotText::parseInvocation(m.text, m.isReply).first;
			 const bool listEnglish = commandEnglish(invoked).value_or(english());
			 if (args.isEmpty()) {
				 say(m.platform, tCmd(invoked, "Texuguito.Bot.AccessoryUsage").arg(m.user));
				 return;
			 }
			 const auto [ok, value] = BotData::validateAccessory(args.join(QLatin1Char(' ')));
			 if (!ok) {
				 say(m.platform,
				     tCmd(invoked, "Texuguito.Bot.AccessoryInvalid")
					     .arg(m.user,
						  BotData::accessoryNames(listEnglish).join(QStringLiteral(", "))));
				 return;
			 }
			 for (const QString &k : lookKeys(key))
				 m_viewers.setAccessory(k, value);
			 updated(key);
		 }},
		{QStringLiteral("apelido"),
		 {QStringLiteral("nick"), QStringLiteral("nickname")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
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
			 updated(key);
		 }},
		{QStringLiteral("dança"),
		 {QStringLiteral("danca"), QStringLiteral("dance")},
		 [this, updated](const BotMessage &, const QString &key, const QStringList &) {
			 m_status[key].dancingUntil = m_clock.elapsed() + kDanceMs;
			 updated(key);
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
		 {QStringLiteral("join")},
		 /* Silent on purpose: a reply per participant would flood the chat. */
		 [this](const BotMessage &, const QString &key, const QStringList &) {
			 m_raffle.join(key);
		 }},
	};
}
