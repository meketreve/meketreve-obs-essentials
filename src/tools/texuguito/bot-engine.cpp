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
#include <QJsonArray>
#include <QRegularExpression>
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

QString noOverlayReply()
{
	return QStringLiteral(
		"🔇 O overlay não está aberto, então não tem onde tocar o áudio. Nenhum ponto foi cobrado.");
}

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

BotEngine::BotEngine(const QString &dataDir, const QString &audioDir, QObject *parent)
	: QObject(parent),
	  m_viewers(dataDir + QStringLiteral("/viewers.json")),
	  m_points(dataDir + QStringLiteral("/points.json")),
	  m_custom(dataDir + QStringLiteral("/custom_commands.json")),
	  m_audioDir(audioDir)
{
	m_clock.start();
	registerCommands();
	reloadClips();

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
	case ChatPlatform::TikTok:
		return QStringLiteral("tt:") + name;
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
	const QDir root(m_audioDir);
	if (!root.exists())
		QDir().mkpath(m_audioDir);
	/* <audio dir>/<cost>/<name>.<ext>: the folder name is the price. */
	for (const QString &folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		bool numeric = false;
		const int cost = folder.toInt(&numeric);
		if (!numeric || cost < 0)
			continue;
		const QDir dir(root.filePath(folder));
		for (const QFileInfo &file : dir.entryInfoList(QDir::Files, QDir::Name)) {
			const QString ext = file.suffix().toLower();
			if (ext != QLatin1String("mp3") && ext != QLatin1String("wav") && ext != QLatin1String("ogg"))
				continue;
			const QString name = file.completeBaseName().toLower();
			const QString relative = folder + QLatin1Char('/') + file.fileName();
			m_clips[name] = AudioClip{name, cost,
						  QStringLiteral("/audios/") +
							  QString::fromLatin1(QUrl::toPercentEncoding(relative, "/"))};
		}
	}
	return static_cast<int>(m_clips.size());
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
	const Status status = m_status.value(key);
	const qint64 now = m_clock.elapsed();
	const auto remaining = [now](qint64 until) {
		return std::max<qint64>(0, until - now) / 1000.0;
	};
	const auto optional = [](const QString &s) {
		return s.isEmpty() ? QJsonValue() : QJsonValue(s);
	};
	static const char *const platforms[] = {"twitch", "youtube", "kick", "tiktok"};
	return QJsonObject{{QStringLiteral("username"), key},
			   {QStringLiteral("nick"), v.nick.isEmpty() ? displayName(key) : v.nick},
			   {QStringLiteral("cor"), v.cor},
			   {QStringLiteral("chapeu"), optional(v.chapeu)},
			   {QStringLiteral("acessorio"), optional(v.acessorio)},
			   {QStringLiteral("is_mod"), status.isMod},
			   {QStringLiteral("is_sub"), status.isSub},
			   {QStringLiteral("is_broadcaster"), status.isBroadcaster},
			   {QStringLiteral("platform"), QLatin1String(platforms[static_cast<int>(status.platform)])},
			   /* Seconds left, not booleans: the overlay turns them into
			    * its own deadlines and checks them every frame. */
			   {QStringLiteral("dance_remaining"), remaining(status.dancingUntil)},
			   {QStringLiteral("cheer_remaining"), remaining(status.cheerUntil)}};
}

void BotEngine::viewerEvent(const QString &type, const QString &key)
{
	/* The overlay only knows present viewers; an update for an absent one
	 * would draw an avatar that never gets a "left". */
	if (type != QLatin1String("left") && !m_status.value(key).present)
		return;
	emit overlayMessage(
		QJsonObject{{QStringLiteral("type"), type},
			    {QStringLiteral("username"), key},
			    {QStringLiteral("viewer"),
			     type == QLatin1String("left") ? QJsonValue() : QJsonValue(viewerPayload(key))}});
}

QJsonObject BotEngine::snapshot()
{
	QJsonArray present;
	for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
		if (it->present)
			present.append(viewerPayload(it.key()));
	}
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")}, {QStringLiteral("viewers"), present}};
}

void BotEngine::refreshPresence()
{
	const qint64 now = m_clock.elapsed();
	for (auto it = m_status.begin(); it != m_status.end(); ++it) {
		Status &s = it.value();
		const bool present = s.inChatters || (s.lastSeen > 0 && now - s.lastSeen < kPresenceMinutes * 60000LL);
		if (present == s.present)
			continue;
		s.present = present;
		viewerEvent(present ? QStringLiteral("joined") : QStringLiteral("left"), it.key());
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
	viewerEvent(QStringLiteral("updated"), key);
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
	s.present = true;
	viewerEvent(QStringLiteral("updated"), key);

	const auto [name, args] = BotText::parseInvocation(msg.text, msg.isReply);
	if (name.isEmpty())
		return;

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

QString BotEngine::handleCustomCommand(const BotMessage &msg, const QString &name, const QStringList &args)
{
	QString response = m_custom.get(name);
	response.replace(QStringLiteral("{user}"), msg.user);
	response.replace(QStringLiteral("{usuario}"), msg.user);
	response.replace(QStringLiteral("{args}"), args.join(QLatin1Char(' ')));
	return response;
}

QString BotEngine::handleComando(const BotMessage &msg, const QStringList &args)
{
	const QString action = args.isEmpty() ? QString() : args[0].toLower();
	const bool priv = privileged(msg);

	if (kList.contains(action) || (action.isEmpty() && !priv)) {
		const QStringList names = m_custom.names();
		if (names.isEmpty())
			return QStringLiteral("📝 Nenhum comando personalizado ainda.");
		QStringList bang;
		for (const QString &n : names)
			bang.append(QLatin1Char('!') + n);
		return BotText::truncate(QStringLiteral("📝 Comandos personalizados: ") +
					 bang.join(QStringLiteral(", ")));
	}
	if (!priv)
		return QString();

	const auto normalize = [](QString raw) {
		while (raw.startsWith(QLatin1Char('!')))
			raw.remove(0, 1);
		return raw.toLower();
	};
	const auto nameError = [](const QString &name) -> QString {
		static const QRegularExpression valid(QStringLiteral("^[\\w-]+$"),
						      QRegularExpression::UseUnicodePropertiesOption);
		if (name.isEmpty())
			return QStringLiteral("❌ Faltou o nome do comando.");
		if (name.size() > kCustomNameMax)
			return QStringLiteral("❌ O nome do comando pode ter no máximo %1 caracteres.")
				.arg(kCustomNameMax);
		if (!valid.match(name).hasMatch())
			return QStringLiteral("❌ O nome do comando só pode ter letras, números, _ e -.");
		return QString();
	};

	if (kAdd.contains(action) || kEdit.contains(action)) {
		if (args.size() < 3)
			return QStringLiteral("❌ Use: !comando %1 <nome> <resposta>")
				.arg(action.isEmpty() ? QStringLiteral("add") : action);
		const QString name = normalize(args[1]);
		const QString error = nameError(name);
		if (!error.isEmpty())
			return error;
		if (reservedNames().contains(name))
			return QStringLiteral("❌ '!%1' é um comando do bot e não pode ser substituído.").arg(name);
		const bool exists = m_custom.contains(name);
		if (kAdd.contains(action) && exists)
			return QStringLiteral("❌ '!%1' já existe. Use !comando edit %1 <resposta> para mudar.")
				.arg(name);
		if (kEdit.contains(action) && !exists)
			return QStringLiteral("❌ '!%1' não existe. Use !comando add %1 <resposta>.").arg(name);
		const QString response = args.mid(2).join(QLatin1Char(' '));
		if (response.size() > kCustomResponseMax)
			return QStringLiteral("❌ A resposta pode ter no máximo %1 caracteres.").arg(kCustomResponseMax);
		m_custom.set(name, response);
		return QStringLiteral("✅ Comando !%1 %2!")
			.arg(name, exists ? QStringLiteral("atualizado") : QStringLiteral("criado"));
	}

	if (kRemove.contains(action)) {
		if (args.size() < 2)
			return QStringLiteral("❌ Use: !comando del <nome>");
		const QString name = normalize(args[1]);
		if (!m_custom.remove(name))
			return QStringLiteral("❌ '!%1' não existe.").arg(name);
		return QStringLiteral("🗑️ Comando !%1 removido.").arg(name);
	}

	return QStringLiteral("❌ Use: !comando add <nome> <resposta> | !comando edit <nome> <resposta> | "
			      "!comando del <nome> | !comando list");
}

void BotEngine::playTts(const BotMessage &msg, const QString &key, const QStringList &args)
{
	if (args.isEmpty()) {
		say(msg.platform, QStringLiteral("❌ Use: !falar <mensagem>"));
		return;
	}
	if (m_listeners <= 0) {
		say(msg.platform, noOverlayReply());
		return;
	}
	if (!m_tts || !m_points.spend(key, kTtsCost)) {
		say(msg.platform, QStringLiteral("❌ Pontos insuficientes (%1 pts necessários).").arg(kTtsCost));
		return;
	}
	const QString text = QStringLiteral("%1 enviou a mensagem: %2").arg(msg.user, args.join(QLatin1Char(' ')));
	const ChatPlatform platform = msg.platform;
	const QString user = msg.user;
	QPointer<BotEngine> self = this;
	m_tts(text, [self, platform, user, key](QByteArray mp3, QString error) {
		if (!self)
			return;
		if (mp3.isEmpty()) {
			/* Nothing was played: give the points back. */
			self->m_points.add(key, kTtsCost);
			self->say(platform, QStringLiteral("❌ Erro ao gerar o TTS. Seus pontos foram devolvidos."));
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
		self->say(platform,
			  QStringLiteral("🎙️ [TTS] %1 enviou uma mensagem! (-%2 pts)").arg(user).arg(kTtsCost));
	});
}

void BotEngine::registerCommands()
{
	const auto updated = [this](const QString &key) {
		viewerEvent(QStringLiteral("updated"), key);
	};

	m_commands = {
		{QStringLiteral("cor"),
		 {QStringLiteral("color")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty()) {
				 say(m.platform, QStringLiteral("@%1 uso: !cor <nome ou hex>").arg(m.user));
				 return;
			 }
			 const auto cor = BotData::validateColor(args.join(QLatin1Char(' ')));
			 if (!cor) {
				 say(m.platform,
				     QStringLiteral("@%1 cor inválida. Use um nome em português (ex: azul) ou "
						    "inglês (ex: blue) ou hex (#rrggbb).")
					     .arg(m.user));
				 return;
			 }
			 m_viewers.setColor(key, *cor);
			 updated(key);
		 }},
		{QStringLiteral("resetcor"),
		 {QStringLiteral("resetcolor")},
		 [this, updated](const BotMessage &, const QString &key, const QStringList &) {
			 m_viewers.resetColor(key);
			 updated(key);
		 }},
		{QStringLiteral("chapeu"),
		 {QStringLiteral("hat")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty()) {
				 say(m.platform,
				     QStringLiteral("@%1 uso: !chapeu <boné|coroa|chifres|nenhum>").arg(m.user));
				 return;
			 }
			 const auto [ok, value] = BotData::validateHat(args.join(QLatin1Char(' ')));
			 if (!ok) {
				 say(m.platform, QStringLiteral("@%1 chapéu inválido. Opções: %2.")
							 .arg(m.user, BotData::hats().join(QStringLiteral(", "))));
				 return;
			 }
			 m_viewers.setHat(key, value);
			 updated(key);
		 }},
		{QStringLiteral("acessorio"),
		 {QStringLiteral("accessory")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty()) {
				 say(m.platform,
				     QStringLiteral("@%1 uso: !acessorio <óculos|capa|asas|nenhum>").arg(m.user));
				 return;
			 }
			 const auto [ok, value] = BotData::validateAccessory(args.join(QLatin1Char(' ')));
			 if (!ok) {
				 say(m.platform,
				     QStringLiteral("@%1 acessório inválido. Opções: %2.")
					     .arg(m.user, BotData::accessories().join(QStringLiteral(", "))));
				 return;
			 }
			 m_viewers.setAccessory(key, value);
			 updated(key);
		 }},
		{QStringLiteral("apelido"),
		 {QStringLiteral("nick"), QStringLiteral("nickname")},
		 [this, updated](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty()) {
				 say(m.platform, QStringLiteral("@%1 uso: !apelido <nome>").arg(m.user));
				 return;
			 }
			 const QString nick = BotData::validateNick(args.join(QLatin1Char(' ')));
			 if (nick.isEmpty()) {
				 say(m.platform, QStringLiteral("@%1 apelido inválido.").arg(m.user));
				 return;
			 }
			 m_viewers.setNick(key, nick);
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
			 if (args.size() < 2) {
				 say(m.platform, QStringLiteral("@%1 uso: !avatarmod <usuario> <cor>").arg(m.user));
				 return;
			 }
			 const auto cor = BotData::validateColor(args.mid(1).join(QLatin1Char(' ')));
			 if (!cor) {
				 say(m.platform, QStringLiteral("@%1 cor inválida.").arg(m.user));
				 return;
			 }
			 QString target = args[0];
			 while (target.startsWith(QLatin1Char('@')))
				 target.remove(0, 1);
			 const QString targetKey = keyFor(m.platform, target);
			 m_viewers.setColor(targetKey, *cor);
			 updated(targetKey);
		 }},
		{QStringLiteral("comandos"),
		 {QStringLiteral("ajuda"), QStringLiteral("help"), QStringLiteral("commands")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 QStringList list{QStringLiteral("!cor <cor>"),
					  QStringLiteral("!resetcor"),
					  QStringLiteral("!chapeu <opção>"),
					  QStringLiteral("!acessorio <opção>"),
					  QStringLiteral("!apelido <nome>"),
					  QStringLiteral("!dança"),
					  QStringLiteral("!pontos"),
					  QStringLiteral("!tocar <nome>"),
					  QStringLiteral("!audios"),
					  QStringLiteral("!falar <msg>"),
					  QStringLiteral("!parar"),
					  QStringLiteral("!entrar"),
					  QStringLiteral("!comando lista"),
					  QStringLiteral("!status"),
					  QStringLiteral("!ping")};
			 if (privileged(m))
				 list << QStringLiteral("!avatarmod <usuario> <cor>")
				      << QStringLiteral("!darpontos <usuario> <qtd>") << QStringLiteral("!recarregar")
				      << QStringLiteral("!comando add/edit/del <nome> <resposta>");
			 if (m.isBroadcaster)
				 list << QStringLiteral("!sorteio <pts> <min>");
			 say(m.platform, QStringLiteral("@%1 comandos: %2 (tem alias em inglês)")
						 .arg(m.user, list.join(QStringLiteral(", "))));
		 }},
		{QStringLiteral("comando"),
		 {QStringLiteral("cmd"), QStringLiteral("command")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 say(m.platform, handleComando(m, args));
		 }},
		{QStringLiteral("ping"),
		 {},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 say(m.platform, QStringLiteral("🏓 Pong, %1!").arg(m.user));
		 }},
		{QStringLiteral("pontos"),
		 {QStringLiteral("pts"), QStringLiteral("points")},
		 [this](const BotMessage &m, const QString &key, const QStringList &) {
			 say(m.platform,
			     QStringLiteral("🪙 %1, você tem %2 pontos.").arg(m.user).arg(m_points.get(key)));
		 }},
		{QStringLiteral("darpontos"),
		 {QStringLiteral("dar"), QStringLiteral("addpontos"), QStringLiteral("addpoints"),
		  QStringLiteral("give"), QStringLiteral("givepoints")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!privileged(m))
				 return;
			 bool ok = false;
			 const qint64 amount = args.size() >= 2 ? args[1].toLongLong(&ok) : 0;
			 if (!ok) {
				 say(m.platform, QStringLiteral("❌ Use: !darpontos <@usuario> <quantidade>"));
				 return;
			 }
			 QString target = args[0].toLower();
			 while (target.startsWith(QLatin1Char('@')))
				 target.remove(0, 1);
			 const QString targetKey = keyFor(m.platform, target);
			 m_points.add(targetKey, amount);
			 say(m.platform, QStringLiteral("✅ %1 pontos adicionados para %2! Saldo: %3 pts.")
						 .arg(amount)
						 .arg(target)
						 .arg(m_points.get(targetKey)));
		 }},
		{QStringLiteral("tocar"),
		 {QStringLiteral("p"), QStringLiteral("play")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 if (args.isEmpty()) {
				 say(m.platform, QStringLiteral("❌ Use: !tocar <nome>"));
				 return;
			 }
			 if (m_lastClip.isValid()) {
				 const qint64 left = kClipCooldownSeconds * 1000LL - m_lastClip.elapsed();
				 if (left > 0) {
					 say(m.platform, QStringLiteral("⏳ Cooldown ativo! Aguarde mais %1 segundos.")
								 .arg(left / 1000 + 1));
					 return;
				 }
			 }
			 const QString name = args.join(QLatin1Char(' ')).toLower();
			 const auto clip = m_clips.find(name);
			 if (clip == m_clips.end()) {
				 say(m.platform, QStringLiteral("❌ Áudio '%1' não encontrado.").arg(name));
				 return;
			 }
			 if (m_listeners <= 0) {
				 say(m.platform, noOverlayReply());
				 return;
			 }
			 if (!m_points.spend(key, clip->second.cost)) {
				 say(m.platform, QStringLiteral("❌ Pontos insuficientes! '%1' custa %2 pts.")
							 .arg(clip->second.name)
							 .arg(clip->second.cost));
				 return;
			 }
			 m_lastClip.start();
			 emit overlayMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio")},
							 {QStringLiteral("url"), clip->second.url},
							 {QStringLiteral("volume"), m_volume}});
			 say(m.platform, QStringLiteral("🔊 Tocando: %1. Saldo: %2 pts.")
						 .arg(clip->second.name)
						 .arg(m_points.get(key)));
		 }},
		{QStringLiteral("falar"),
		 {QStringLiteral("tts"), QStringLiteral("speak")},
		 [this](const BotMessage &m, const QString &key, const QStringList &args) {
			 playTts(m, key, args);
		 }},
		{QStringLiteral("audios"),
		 {QStringLiteral("sons"), QStringLiteral("sounds"), QStringLiteral("audio")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 if (m_clips.empty()) {
				 say(m.platform, QStringLiteral("🔈 Nenhum áudio encontrado nas pastas."));
				 return;
			 }
			 std::map<int, QStringList> byCost;
			 for (const auto &c : m_clips)
				 byCost[c.second.cost].append(c.second.name);
			 QStringList parts;
			 for (auto &entry : byCost) {
				 entry.second.sort();
				 parts.append(QStringLiteral("[%1 pts: %2]")
						      .arg(entry.first)
						      .arg(entry.second.join(QStringLiteral(", "))));
			 }
			 say(m.platform, BotText::truncate(QStringLiteral("🎵 Sons Disponíveis: ") +
							   parts.join(QStringLiteral(" | "))));
		 }},
		{QStringLiteral("parar"),
		 {QStringLiteral("stop")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 emit overlayMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio_stop")}});
			 say(m.platform, QStringLiteral("⏹️ Áudio parado!"));
		 }},
		{QStringLiteral("recarregar"),
		 {QStringLiteral("reload")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 if (privileged(m))
				 say(m.platform, QStringLiteral("🔄 Recarregado! %1 áudios.").arg(reloadClips()));
		 }},
		{QStringLiteral("status"),
		 {QStringLiteral("estado")},
		 [this](const BotMessage &m, const QString &, const QStringList &) {
			 say(m.platform, QStringLiteral("📊 [STATUS] Texuguito está online! 🎵 %1 áudios carregados. "
							"🪙 Sistema de pontos ativo.")
						 .arg(m_clips.size()));
		 }},
		{QStringLiteral("sorteio"),
		 {QStringLiteral("raffle")},
		 [this](const BotMessage &m, const QString &, const QStringList &args) {
			 if (!m.isBroadcaster)
				 return;
			 if (m_raffle.active()) {
				 say(m.platform, QStringLiteral("❌ Já existe um sorteio em andamento!"));
				 return;
			 }
			 const qint64 prize = args.size() >= 2 ? args[0].toLongLong() : 0;
			 const int minutes = args.size() >= 2 ? args[1].toInt() : 0;
			 if (prize <= 0 || minutes <= 0) {
				 say(m.platform, QStringLiteral("❌ Use: !sorteio <pontos> <minutos>"));
				 return;
			 }
			 m_raffle.start(prize);
			 say(m.platform,
			     QStringLiteral("🎉 [SORTEIO] Um sorteio de %1 pontos começou! Digite !entrar para "
					    "participar. Tempo: %2 min.")
				     .arg(prize)
				     .arg(minutes));
			 const ChatPlatform platform = m.platform;
			 QTimer::singleShot(minutes * 60000, this, [this, platform]() {
				 const auto [winner, won] = m_raffle.finish(m_points);
				 if (winner.isEmpty())
					 say(platform,
					     QStringLiteral("⚠️ O sorteio terminou, mas não houve participantes."));
				 else
					 say(platform,
					     QStringLiteral("🎊 PARABÉNS @%1! Você ganhou o sorteio de %2 pontos! 🥳")
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
