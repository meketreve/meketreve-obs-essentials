/*
Meketreve OBS Essentials - Texuguito bot unit tests (developer tool)
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
#include "overlay-server.hpp"
#include "sound-fetch.hpp"
#include "tts-client.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

Q_DECLARE_METATYPE(ChatPlatform)

namespace {

BotMessage msg(const QString &user, const QString &text, ChatPlatform p = ChatPlatform::Twitch)
{
	BotMessage m;
	m.platform = p;
	m.user = user;
	m.text = text;
	return m;
}

QStringList replies(QSignalSpy &spy)
{
	QStringList out;
	for (const QList<QVariant> &args : spy)
		out.append(args.at(1).toString());
	return out;
}

/* A plugin locale file (Key="value" lines), as OBS would read it. */
QHash<QString, QString> readLocale(const char *name)
{
	QHash<QString, QString> texts;
	QFile file(QStringLiteral(MEKETREVE_LOCALE_DIR "/") + QLatin1String(name));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return texts;
	while (!file.atEnd()) {
		const QString line = QString::fromUtf8(file.readLine()).trimmed();
		const qsizetype eq = line.indexOf(QLatin1Char('='));
		if (eq <= 0 || line.startsWith(QLatin1Char('#')))
			continue;
		QString value = line.mid(eq + 1);
		if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
			value = value.mid(1, value.size() - 2);
		texts.insert(line.left(eq), value.replace(QStringLiteral("\\\""), QStringLiteral("\"")));
	}
	return texts;
}

BotEngine::TextFunction locale(const char *name)
{
	const QHash<QString, QString> texts = readLocale(name);
	return [texts](const char *key) {
		return texts.value(QLatin1String(key), QLatin1String(key));
	};
}

void writeFile(const QString &path, const QByteArray &data)
{
	QFile f(path);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(data);
}

} // namespace

class TestTexuguito : public QObject {
	Q_OBJECT

private slots:
	void initTestCase() { qRegisterMetaType<ChatPlatform>(); }

	void validators()
	{
		QCOMPARE(BotData::validateColor(QStringLiteral("azul")).value(), QStringLiteral("#0000ff"));
		QCOMPARE(BotData::validateColor(QStringLiteral("Blue")).value(), QStringLiteral("#0000ff"));
		QCOMPARE(BotData::validateColor(QStringLiteral("verde limão")).value(), QStringLiteral("#00ff00"));
		QCOMPARE(BotData::validateColor(QStringLiteral("#ABCDEF")).value(), QStringLiteral("#abcdef"));
		QVERIFY(!BotData::validateColor(QStringLiteral("azulzinho")));
		QCOMPARE(BotData::validateHat(QStringLiteral("crown")), std::make_pair(true, QStringLiteral("coroa")));
		QCOMPARE(BotData::validateHat(QStringLiteral("nenhum")), std::make_pair(true, QString()));
		QVERIFY(!BotData::validateHat(QStringLiteral("sombrero")).first);
		QCOMPARE(BotData::validateAccessory(QStringLiteral("bat wings")).second, QStringLiteral("asasmorcego"));
		QCOMPARE(BotData::validateNick(QStringLiteral("  João_2!!<script>  ")), QStringLiteral("João_2script"));
		QCOMPARE(BotData::validateNick(QStringLiteral("!!!")), QString());
		QCOMPARE(BotData::validateNick(QStringLiteral("abcdefghijklmnopqrstu")).size(), 16);
	}

	void readsPythonFilesAndQuarantinesBrokenOnes()
	{
		QTemporaryDir dir;
		/* Files as the Python bot wrote them (moc cannot read raw strings with braces). */
		const QJsonObject points{{QStringLiteral("Fulano"), 120}, {QStringLiteral("beltrano"), 5}};
		writeFile(dir.filePath(QStringLiteral("points.json")), QJsonDocument(points).toJson());
		const QJsonObject fulano{{QStringLiteral("cor"), QStringLiteral("#ff0000")},
					 {QStringLiteral("chapeu"), QStringLiteral("coroa")},
					 {QStringLiteral("acessorio"), QJsonValue()},
					 {QStringLiteral("nick"), QStringLiteral("Ful")},
					 {QStringLiteral("primeira_vez"), QStringLiteral("2026-01-01T00:00:00+00:00")},
					 {QStringLiteral("ultima_vez"), QStringLiteral("2026-01-01T00:00:00+00:00")}};
		writeFile(dir.filePath(QStringLiteral("viewers.json")),
			  QJsonDocument(QJsonObject{{QStringLiteral("fulano"), fulano}}).toJson());
		writeFile(dir.filePath(QStringLiteral("custom_commands.json")), "this is not json");

		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QCOMPARE(bot.points().get(QStringLiteral("FULANO")), 120);
		QCOMPARE(bot.viewers().getOrCreate(QStringLiteral("fulano")).chapeu, QStringLiteral("coroa"));
		QVERIFY(QFile::exists(dir.filePath(QStringLiteral("custom_commands.json.corrupt"))));
		QVERIFY(bot.customCommands().names().isEmpty());
	}

	void keysPerPlatform()
	{
		QCOMPARE(BotEngine::keyFor(ChatPlatform::Twitch, QStringLiteral("Fulano")), QStringLiteral("fulano"));
		QCOMPARE(BotEngine::keyFor(ChatPlatform::Kick, QStringLiteral("Fulano")),
			 QStringLiteral("kick:fulano"));
		QCOMPARE(BotEngine::keyFor(ChatPlatform::YouTube, QStringLiteral("Ful")), QStringLiteral("yt:ful"));
	}

	void avatarCommandsUpdateOverlay()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);
		QSignalSpy said(&bot, &BotEngine::reply);

		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!color blue")));
		QCOMPARE(bot.viewers().getOrCreate(QStringLiteral("ana")).cor, QStringLiteral("#0000ff"));
		const QJsonObject last = overlay.last().at(0).toJsonObject();
		QCOMPARE(last.value(QStringLiteral("type")).toString(), QStringLiteral("updated"));
		QCOMPARE(last.value(QStringLiteral("viewer")).toObject().value(QStringLiteral("cor")).toString(),
			 QStringLiteral("#0000ff"));

		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!hat top hat")));
		QCOMPARE(bot.viewers().getOrCreate(QStringLiteral("ana")).chapeu, QStringLiteral("cartola"));
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!apelido Aninha")));
		QCOMPARE(overlay.last()
				 .at(0)
				 .toJsonObject()
				 .value(QStringLiteral("viewer"))
				 .toObject()
				 .value(QStringLiteral("nick")),
			 QJsonValue(QStringLiteral("Aninha")));
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!cor azulzinho")));
		QVERIFY(replies(said).last().contains(QStringLiteral("cor inválida")));
	}

	void replyMessagesSkipTheMention()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		BotMessage m = msg(QStringLiteral("Ana"), QStringLiteral("@Beto !apelido Nova"));
		m.isReply = true;
		bot.handleMessage(m);
		QCOMPARE(bot.viewers().getOrCreate(QStringLiteral("ana")).nick, QStringLiteral("Nova"));
	}

	void pointsAndPrivileges()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy said(&bot, &BotEngine::reply);

		bot.handleMessage(msg(QStringLiteral("viewer"), QStringLiteral("!darpontos @viewer 999")));
		QCOMPARE(bot.points().get(QStringLiteral("viewer")), 0);
		QVERIFY(said.isEmpty());

		BotMessage mod = msg(QStringLiteral("Mod"), QStringLiteral("!give @Viewer 50"));
		mod.isMod = true;
		bot.handleMessage(mod);
		QCOMPARE(bot.points().get(QStringLiteral("viewer")), 50);
		QVERIFY(replies(said).last().startsWith(QStringLiteral("✅ 50 pontos adicionados para viewer")));

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!pts")));
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, você tem 50 pontos."));

		/* Same name on Kick is a different person. */
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!pontos"), ChatPlatform::Kick));
		QCOMPARE(said.last().at(0).value<ChatPlatform>(), ChatPlatform::Kick);
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, você tem 0 pontos."));
	}

	void soundboardCostsCooldownAndOverlay()
	{
		QTemporaryDir dir;
		const QString audio = dir.filePath(QStringLiteral("audios"));
		QDir().mkpath(audio + QStringLiteral("/10"));
		QDir().mkpath(audio + QStringLiteral("/grátis"));
		writeFile(audio + QStringLiteral("/10/Buzina Alta.mp3"), "x");
		writeFile(audio + QStringLiteral("/grátis/ignorado.mp3"), "x");
		BotEngine bot(dir.path(), audio);
		bot.setText(locale("pt-BR.ini"));
		QCOMPARE(bot.clips().size(), size_t(1));
		QCOMPARE(bot.clips().begin()->second.url, QStringLiteral("/audios/10/Buzina%20Alta.mp3"));

		QSignalSpy said(&bot, &BotEngine::reply);
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);
		bot.points().add(QStringLiteral("ana"), 25);

		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!p buzina alta")));
		QVERIFY(replies(said).last().startsWith(QStringLiteral("🔇")));
		QCOMPARE(bot.points().get(QStringLiteral("ana")), 25);

		bot.setOverlayListeners(1);
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QCOMPARE(replies(said).last(), QStringLiteral("🔊 Tocando: buzina alta. Saldo: 15 pts."));
		QCOMPARE(overlay.last().at(0).toJsonObject().value(QStringLiteral("type")).toString(),
			 QStringLiteral("audio"));

		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QVERIFY(replies(said).last().startsWith(QStringLiteral("⏳ Os sons de 10 pts estão em espera!")));

		bot.resetClipCooldown();
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QCOMPARE(replies(said).last(), QStringLiteral("🔊 Tocando: buzina alta. Saldo: 5 pts."));
		bot.resetClipCooldown();
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Pontos insuficientes! 'buzina alta' custa 10 pts."));

		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!sons")));
		QCOMPARE(replies(said).last(), QStringLiteral("🎵 Sons Disponíveis: [10 pts: buzina alta]"));
	}

	void cooldownPerPrice()
	{
		QTemporaryDir dir;
		const QString audio = dir.filePath(QStringLiteral("audios"));
		QDir().mkpath(audio + QStringLiteral("/20"));
		QDir().mkpath(audio + QStringLiteral("/200"));
		writeFile(audio + QStringLiteral("/20/pato.mp3"), "x");
		writeFile(audio + QStringLiteral("/20/sino.mp3"), "x");
		writeFile(audio + QStringLiteral("/200/trovao.mp3"), "x");
		BotEngine bot(dir.path(), audio);
		bot.setText(locale("pt-BR.ini"));
		bot.setOverlayListeners(1);
		bot.points().add(QStringLiteral("ana"), 1000);
		QCOMPARE(bot.clipCosts(), (QList<int>{20, 200}));
		QCOMPARE(bot.clipCooldownSeconds(20), 10);
		QCOMPARE(bot.clipCooldownSeconds(200), 60);
		QCOMPARE(bot.clipCooldownSeconds(500), 120);
		QSignalSpy said(&bot, &BotEngine::reply);

		/* An expensive sound does not hold back a cheap one... */
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar trovao")));
		QVERIFY(replies(said).last().startsWith(QStringLiteral("🔊 Tocando: trovao")));
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar pato")));
		QVERIFY(replies(said).last().startsWith(QStringLiteral("🔊 Tocando: pato")));
		/* ...but the same price waits, even for another sound. */
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar sino")));
		QVERIFY(replies(said).last().startsWith(
			QStringLiteral("⏳ Os sons de 20 pts estão em espera! Aguarde mais 1")));
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar trovao")));
		QVERIFY(replies(said).last().contains(QStringLiteral("Aguarde mais 6")));

		/* The streamer's own wait replaces the default; 0 turns it off. */
		bot.setClipCooldowns({{20, 0}});
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar sino")));
		QVERIFY(replies(said).last().startsWith(QStringLiteral("🔊 Tocando: sino")));
		QCOMPARE(bot.clipCooldownSeconds(200), 60);
	}

	void ttsChargesAndRefunds()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		bot.setOverlayListeners(1);
		bot.points().add(QStringLiteral("ana"), 450);
		QString spoken, voice;
		bool fail = false;
		bot.setTts(
			[&](const QString &text, const QString &lang, std::function<void(QByteArray, QString)> done) {
				spoken = text;
				voice = lang;
				done(fail ? QByteArray() : QByteArray("ID3mp3"), QString());
			});
		QSignalSpy said(&bot, &BotEngine::reply);
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);

		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!falar olá chat")));
		QCOMPARE(spoken, QStringLiteral("Ana enviou a mensagem: olá chat"));
		QCOMPARE(voice, QStringLiteral("pt"));
		QCOMPARE(bot.points().get(QStringLiteral("ana")), 250);
		const QString url = overlay.last().at(0).toJsonObject().value(QStringLiteral("url")).toString();
		QVERIFY(url.startsWith(QStringLiteral("/tts/")));
		QCOMPARE(bot.ttsClip(url.mid(5)), QByteArray("ID3mp3"));

		fail = true;
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!tts de novo")));
		QCOMPARE(bot.points().get(QStringLiteral("ana")), 250);
		QVERIFY(replies(said).last().contains(QStringLiteral("devolvidos")));

		fail = false;
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!speak caro")));
		QCOMPARE(bot.points().get(QStringLiteral("ana")), 50);
		QCOMPARE(spoken, QStringLiteral("Ana sent the message: caro"));
		QCOMPARE(voice, QStringLiteral("en"));
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!speak caro")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Pontos insuficientes (200 pts necessários)."));
	}

	void customCommands()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy said(&bot, &BotEngine::reply);
		BotMessage mod = msg(QStringLiteral("Mod"), QString());
		mod.isMod = true;

		mod.text = QStringLiteral("!comando add !Discord entra aí {user}: discord.gg/x {args}");
		bot.handleMessage(mod);
		QCOMPARE(replies(said).last(), QStringLiteral("✅ Comando !discord criado!"));
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!discord agora")));
		QCOMPARE(replies(said).last(), QStringLiteral("entra aí Ana: discord.gg/x agora"));

		mod.text = QStringLiteral("!cmd add pts roubado");
		bot.handleMessage(mod);
		QCOMPARE(replies(said).last(),
			 QStringLiteral("❌ '!pts' é um comando do bot e não pode ser substituído."));

		/* Only mods manage commands; a viewer gets no answer. */
		const qsizetype before = said.size();
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!comando add hack x")));
		QCOMPARE(said.size(), before);
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!comando")));
		QCOMPARE(replies(said).last(), QStringLiteral("📝 Comandos personalizados: !discord"));

		mod.text = QStringLiteral("!command del discord");
		bot.handleMessage(mod);
		QCOMPARE(replies(said).last(), QStringLiteral("🗑️ Comando !discord removido."));
	}

	void presenceAndPointsTick()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("oi")));
		bot.setTwitchChatters({QStringLiteral("quieto")});
		const QJsonArray viewers = bot.snapshot().value(QStringLiteral("viewers")).toArray();
		QCOMPARE(viewers.size(), 2);

		bot.pointsTick();
		QCOMPARE(bot.points().get(QStringLiteral("quieto")), 0);
		bot.pointsTick();
		QCOMPARE(bot.points().get(QStringLiteral("quieto")), 1);
		QCOMPARE(bot.points().get(QStringLiteral("ana")), 1);

		bot.setTwitchChatters({});
		QCOMPARE(overlay.last().at(0).toJsonObject().value(QStringLiteral("type")).toString(),
			 QStringLiteral("left"));
		QCOMPARE(bot.snapshot().value(QStringLiteral("viewers")).toArray().size(), 1);
	}

	void streamerAlwaysInParade()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		const auto keys = [&bot]() {
			QStringList out;
			for (const QJsonValue v : bot.snapshot().value(QStringLiteral("viewers")).toArray())
				out.append(v.toObject().value(QStringLiteral("username")).toString());
			return out;
		};

		/* Twitch and Kick channels with the same name: one crowned avatar. */
		bot.setStreamerChannels({{ChatPlatform::Twitch, QStringLiteral("meketreve")},
					 {ChatPlatform::Kick, QStringLiteral("meketreve")}});
		QCOMPARE(keys().size(), 1);
		const QJsonObject streamer = bot.snapshot().value(QStringLiteral("viewers")).toArray().at(0).toObject();
		QVERIFY(streamer.value(QStringLiteral("is_broadcaster")).toBool());

		/* Not in the Twitch viewer list and quiet: still there. */
		bot.setTwitchChatters({QStringLiteral("ana")});
		bot.setTwitchChatters({});
		QCOMPARE(keys().size(), 1);
		QVERIFY(keys().first().endsWith(QLatin1String("meketreve")));

		/* A new channel name takes the old one's place. */
		bot.setStreamerChannels({{ChatPlatform::Twitch, QStringLiteral("outro")}});
		QCOMPARE(keys(), QStringList{QStringLiteral("outro")});

		/* The broadcaster badge (YouTube) keeps them too. */
		BotMessage owner = msg(QStringLiteral("Dono"), QStringLiteral("oi"), ChatPlatform::YouTube);
		owner.isBroadcaster = true;
		bot.handleMessage(owner);
		bot.setStreamerChannels({});
		QCOMPARE(keys(), QStringList{QStringLiteral("yt:dono")});
	}

	void streamerCommandToEveryChatAnswersOnce()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		bot.setStreamerChannels({{ChatPlatform::Twitch, QStringLiteral("meketreve")},
					 {ChatPlatform::Kick, QStringLiteral("meketreve")}});
		QSignalSpy said(&bot, &BotEngine::reply);

		/* Sent to every chat: it comes back from Twitch, Kick and YouTube. */
		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!pontos")));
		bot.handleMessage(msg(QStringLiteral("meketreve"), QStringLiteral("!pontos"), ChatPlatform::Kick));
		BotMessage owner = msg(QStringLiteral("Meketreve"), QStringLiteral("!pontos"), ChatPlatform::YouTube);
		owner.isBroadcaster = true;
		bot.handleMessage(owner);
		QCOMPARE(said.size(), 1);
		QCOMPARE(said.at(0).at(0).value<ChatPlatform>(), ChatPlatform::Twitch);

		/* Typed again in one chat, or a different command: answered. */
		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!pontos")));
		bot.handleMessage(msg(QStringLiteral("meketreve"), QStringLiteral("!audios"), ChatPlatform::Kick));
		QCOMPARE(said.size(), 3);

		/* Viewers with the same name on two platforms are two people. */
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!pontos")));
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!pontos"), ChatPlatform::Kick));
		QCOMPARE(said.size(), 5);
	}

	void soundFetchHelpers()
	{
		using namespace SoundFetch;
		QVERIFY(allowedUrl(QUrl(QStringLiteral("https://www.myinstants.com/pt/instant/x/"))));
		QVERIFY(!allowedUrl(QUrl(QStringLiteral("http://www.myinstants.com/media/sounds/x.mp3"))));
		QVERIFY(!allowedUrl(QUrl(QStringLiteral("https://localhost:8902/x.mp3"))));
		QVERIFY(!allowedUrl(QUrl(QStringLiteral("https://127.0.0.1/x.mp3"))));
		QVERIFY(!allowedUrl(QUrl(QStringLiteral("https://192.168.0.10/x.mp3"))));
		QVERIFY(!allowedUrl(QUrl(QStringLiteral("file:///etc/passwd"))));

		QCOMPARE(audioType(QByteArray("ID3\x04", 4)), QStringLiteral("mp3"));
		QCOMPARE(audioType(QByteArray("\xFF\xFB\x90", 3)), QStringLiteral("mp3"));
		QCOMPARE(audioType(QByteArray("OggS")), QStringLiteral("ogg"));
		QCOMPARE(audioType(QByteArray("RIFF\0\0\0\0WAVE", 12)), QStringLiteral("wav"));
		QVERIFY(audioType(QByteArray("<!DOCTYPE html>")).isEmpty());

		const QUrl page(QStringLiteral("https://www.myinstants.com/pt/instant/vine-boom-sound-70972/"));
		QCOMPARE(audioUrlInPage("<button onclick=\"play('/media/sounds/vine-boom.mp3', 'loader')\">", page),
			 QUrl(QStringLiteral("https://www.myinstants.com/media/sounds/vine-boom.mp3")));
		QCOMPARE(audioUrlInPage("<meta property=\"og:audio\" content=\"https://cdn.x.com/a.ogg\">"
					"<a href=\"/b.mp3\">",
					page),
			 QUrl(QStringLiteral("https://cdn.x.com/a.ogg")));
		QVERIFY(audioUrlInPage("<p>nothing here</p>", page).isEmpty());

		QCOMPARE(clipName(QStringLiteral("Ação Épica!! (1)")), QStringLiteral("acao-epica-1"));
		QCOMPARE(clipName(QStringLiteral("vine_boom")), QStringLiteral("vine_boom"));
		QCOMPARE(clipName(QStringLiteral("../../etc")), QStringLiteral("etc"));
		QCOMPARE(clipName(QString(40, QLatin1Char('a'))).size(), 32);
		QCOMPARE(nameFromUrl(QUrl(QStringLiteral("https://x.com/media/sounds/Vine Boom.mp3"))),
			 QStringLiteral("vine-boom"));
	}

	void addAudioCommand()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy said(&bot, &BotEngine::reply);
		QStringList links;
		SoundFetch::Result next;
		bot.setSoundFetch(
			[&links, &next](const QString &link, std::function<void(const SoundFetch::Result &)> done) {
				links.append(link);
				done(next);
			});
		const QByteArray mp3("ID3\x04 fake sound", 17);
		next.data = mp3;
		next.ext = QStringLiteral("mp3");
		next.name = QStringLiteral("vine-boom");

		/* Viewers cannot add sounds; nothing is downloaded. */
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!addaudio https://x.com/a.mp3 50")));
		QVERIFY(links.isEmpty());
		QCOMPARE(said.size(), 0);

		BotMessage mod = msg(QStringLiteral("mod"), QStringLiteral("!addaudio https://x.com/Vine.mp3"));
		mod.isMod = true;
		bot.handleMessage(mod); /* no price */
		QVERIFY(replies(said).last().contains(QStringLiteral("!addaudio")));
		QVERIFY(links.isEmpty());

		/* The link keeps its case; the sound lands in audios/<price>/. */
		mod.text = QStringLiteral("!addaudio https://x.com/Vine.mp3 50");
		bot.handleMessage(mod);
		QCOMPARE(links.last(), QStringLiteral("https://x.com/Vine.mp3"));
		QVERIFY(replies(said).last().contains(QStringLiteral("vine-boom")));
		QVERIFY(QFile::exists(dir.filePath(QStringLiteral("audios/50/vine-boom.mp3"))));
		QCOMPARE(bot.clips().at(QStringLiteral("vine-boom")).cost, 50);

		/* Same name again: refused. A name of your own: fine. */
		bot.handleMessage(mod);
		QVERIFY(replies(said).last().contains(QStringLiteral("Já existe")));
		mod.text = QStringLiteral("!addaudio https://x.com/Vine.mp3 100 Bum Alto");
		bot.handleMessage(mod);
		QVERIFY(QFile::exists(dir.filePath(QStringLiteral("audios/100/bum-alto.mp3"))));

		/* The streamer too; a page with no sound is reported. */
		BotMessage owner = msg(QStringLiteral("dono"), QStringLiteral("!addaudio https://x.com/page 10"));
		owner.isBroadcaster = true;
		next = SoundFetch::Result();
		next.error = SoundFetch::Error::NoAudio;
		bot.handleMessage(owner);
		QVERIFY(replies(said).last().contains(QStringLiteral("Não achei")));

		/* Not audio, or a price out of range: nothing saved. */
		QVERIFY(!bot.addClip("<html>", QStringLiteral("mp3"), QStringLiteral("fake"), 10).isEmpty());
		QVERIFY(!bot.addClip(mp3, QStringLiteral("mp3"), QStringLiteral("caro"), -1).isEmpty());
		QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("audios/10/fake.mp3"))));
	}

	void englishReplies()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("en-US.ini"));
		bot.setCommandTexts(readLocale("en-US.ini"), readLocale("pt-BR.ini"));
		QSignalSpy said(&bot, &BotEngine::reply);
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!points")));
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, you have 0 points."));
		/* !chapeu answers in Portuguese even with OBS in English. */
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!chapeu sombrero")));
		QVERIFY(replies(said).last().startsWith(
			QStringLiteral("@Viewer chapéu inválido. Opções: boné, coroa, chifres,")));
		/* The wording and the page follow the command, not OBS. */
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!comandos")));
		QCOMPARE(replies(said).last(),
			 QStringLiteral("@Viewer todos os comandos: ") + QLatin1String(BotEngine::kCommandsUrlPt));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!commands")));
		QVERIFY(replies(said).last().endsWith(QStringLiteral("/commands.html")));
	}

	void commandLanguageFollowsCommand()
	{
		QVERIFY(BotEngine::commandEnglish(QStringLiteral("color")) == std::optional<bool>(true));
		QVERIFY(BotEngine::commandEnglish(QStringLiteral("cor")) == std::optional<bool>(false));
		QVERIFY(BotEngine::commandEnglish(QStringLiteral("help")) == std::optional<bool>(true));
		QVERIFY(BotEngine::commandEnglish(QStringLiteral("ajuda")) == std::optional<bool>(false));
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("ping")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("status")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("pts")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("p")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("tts")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("cmd")).has_value());
		QVERIFY(!BotEngine::commandEnglish(QStringLiteral("audio")).has_value());

		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		/* OBS in Portuguese, but English commands answer in English. */
		bot.setText(locale("pt-BR.ini"));
		bot.setCommandTexts(readLocale("en-US.ini"), readLocale("pt-BR.ini"));
		QSignalSpy said(&bot, &BotEngine::reply);

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!points")));
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, you have 0 points."));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!pontos")));
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, você tem 0 pontos."));
		/* Neutral aliases follow OBS. */
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!pts")));
		QCOMPARE(replies(said).last(), QStringLiteral("🪙 Viewer, você tem 0 pontos."));

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!color")));
		QCOMPARE(replies(said).last(), QStringLiteral("@Viewer usage: !color <name or hex>"));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!cor")));
		QCOMPARE(replies(said).last(), QStringLiteral("@Viewer uso: !cor <nome ou hex>"));

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!play")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Usage: !play <name>"));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!tocar")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Use: !tocar <nome>"));

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!speak")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Usage: !speak <message>"));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!falar")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Use: !falar <mensagem>"));

		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!help")));
		QVERIFY(replies(said).last().endsWith(QStringLiteral("/commands.html")));
		bot.handleMessage(msg(QStringLiteral("Viewer"), QStringLiteral("!ajuda")));
		QVERIFY(replies(said).last().endsWith(QStringLiteral("/comandos.html")));
	}

	void localeFilesHaveTheSameKeys()
	{
		const QHash<QString, QString> en = readLocale("en-US.ini");
		const QHash<QString, QString> pt = readLocale("pt-BR.ini");
		QVERIFY(en.size() > 300);
		QStringList missing;
		for (auto it = en.constBegin(); it != en.constEnd(); ++it)
			if (!pt.contains(it.key()))
				missing.append(QStringLiteral("pt-BR: ") + it.key());
		for (auto it = pt.constBegin(); it != pt.constEnd(); ++it)
			if (!en.contains(it.key()))
				missing.append(QStringLiteral("en-US: ") + it.key());
		QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));
		QCOMPARE(BotData::hatNames(false), BotData::hats());
	}

	void sameNameOnTwoPlatformsIsOneAvatar()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);
		const auto drawnNames = [&bot]() {
			QStringList names;
			for (const QJsonValue v : bot.snapshot().value(QStringLiteral("viewers")).toArray())
				names.append(v.toObject().value(QStringLiteral("username")).toString());
			names.sort();
			return names;
		};

		/* Twitch Ana is in the viewer list (quiet); Kick Ana chats afterwards. */
		bot.setTwitchChatters({QStringLiteral("ana")});
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("oi do kick"), ChatPlatform::Kick));
		bot.handleMessage(msg(QStringLiteral("Bia"), QStringLiteral("oi"), ChatPlatform::Kick));
		QCOMPARE(drawnNames(), (QStringList{QStringLiteral("ana"), QStringLiteral("kick:bia")}));
		/* No "joined" was ever sent for the second Ana. */
		for (const QList<QVariant> &args : overlay)
			QVERIFY(args.at(0).toJsonObject().value(QStringLiteral("username")).toString() !=
				QLatin1String("kick:ana"));

		/* A dance from the Kick Ana moves the one avatar there is. */
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!dança"), ChatPlatform::Kick));
		const QJsonObject last = overlay.last().at(0).toJsonObject();
		QCOMPARE(last.value(QStringLiteral("username")).toString(), QStringLiteral("ana"));
		QVERIFY(last.value(QStringLiteral("viewer"))
				.toObject()
				.value(QStringLiteral("dance_remaining"))
				.toDouble() > 0);

		/* When the Twitch Ana leaves, the Kick one takes the avatar over. */
		bot.setTwitchChatters({});
		QCOMPARE(drawnNames(), (QStringList{QStringLiteral("kick:ana"), QStringLiteral("kick:bia")}));
		QCOMPARE(overlay.at(overlay.size() - 2).at(0).toJsonObject().value(QStringLiteral("type")).toString(),
			 QStringLiteral("left"));
		QCOMPARE(overlay.last().at(0).toJsonObject().value(QStringLiteral("username")).toString(),
			 QStringLiteral("kick:ana"));
	}

	void lookCommandReachesTheDrawnTwin()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		/* Kick is drawn; the streamer changes the look from Twitch. */
		bot.setStreamerChannels({{ChatPlatform::Kick, QStringLiteral("meketreve")}});
		bot.setStreamerChannels({{ChatPlatform::Kick, QStringLiteral("meketreve")},
					 {ChatPlatform::Twitch, QStringLiteral("meketreve")}});
		const auto drawn = [&bot]() {
			return bot.snapshot().value(QStringLiteral("viewers")).toArray().at(0).toObject();
		};
		QCOMPARE(drawn().value(QStringLiteral("username")).toString(), QStringLiteral("kick:meketreve"));

		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!cor vermelho")));
		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!chapeu boné")));
		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!apelido Chefe")));
		bot.handleMessage(msg(QStringLiteral("Meketreve"), QStringLiteral("!dança")));
		const QJsonObject avatar = drawn();
		QCOMPARE(avatar.value(QStringLiteral("username")).toString(), QStringLiteral("kick:meketreve"));
		QCOMPARE(avatar.value(QStringLiteral("cor")).toString(), QStringLiteral("#ff0000"));
		QCOMPARE(avatar.value(QStringLiteral("chapeu")).toString(), QStringLiteral("boné"));
		QCOMPARE(avatar.value(QStringLiteral("nick")).toString(), QStringLiteral("Chefe"));
		QVERIFY(avatar.value(QStringLiteral("dance_remaining")).toDouble() > 0);
	}

	void raffle()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setText(locale("pt-BR.ini"));
		bot.setRaffleChooser([](const QStringList &list) { return list.last(); });
		QSignalSpy said(&bot, &BotEngine::reply);
		bot.handleMessage(msg(QStringLiteral("Mod"), QStringLiteral("!sorteio 100 1")));
		QVERIFY(said.isEmpty());
		BotMessage owner = msg(QStringLiteral("Dona"), QStringLiteral("!raffle 100 0"));
		owner.isBroadcaster = true;
		bot.handleMessage(owner);
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Use: !sorteio <pontos> <minutos>"));
		QVERIFY(bot.points().get(QStringLiteral("zeca")) == 0);
	}

	void ttsHelpers()
	{
		const QStringList chunks =
			GoogleTts::splitText(QString(250, QLatin1Char('a')) + QStringLiteral(" fim"));
		QCOMPARE(chunks.size(), 3);
		QCOMPARE(chunks[0].size(), 100);
		const QByteArray body = GoogleTts::requestBody(QStringLiteral("olá"));
		QVERIFY(body.startsWith("f.req=%5B%5B%5B%22jQ1olc%22"));
		QCOMPARE(GoogleTts::parseResponse(")]}'\n123\n[[\"wrb.fr\",\"jQ1olc\",\"[\\\"SUQz\\\"]\",null]]\n"),
			 QByteArray("ID3"));
		QVERIFY(GoogleTts::parseResponse("nothing").isEmpty());
	}

	void overlayServerServesAndBroadcasts()
	{
		QTemporaryDir web;
		QTemporaryDir audio;
		writeFile(web.filePath(QStringLiteral("overlay.html")), "<html>parade</html>");
		QDir().mkpath(web.filePath(QStringLiteral("assets")));
		writeFile(web.filePath(QStringLiteral("assets/a.json")), "{}");
		QDir().mkpath(audio.filePath(QStringLiteral("10")));
		writeFile(audio.filePath(QStringLiteral("10/som legal.mp3")), "MP3DATA");

		OverlayServer::Routes routes;
		routes.webDir = web.path();
		routes.audioDir = [&audio]() {
			return audio.path();
		};
		routes.ttsClip = [](const QString &id) {
			return id == QLatin1String("abc") ? QByteArray("TTS") : QByteArray();
		};
		routes.snapshot = []() {
			return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")}};
		};
		OverlayServer server(routes);
		QVERIFY(server.listen(0));

		/* The server lives in this thread: pump its events while waiting. */
		QByteArray page;
		QTcpSocket client;
		client.connectToHost(QStringLiteral("127.0.0.1"), server.port());
		QTRY_VERIFY(client.state() == QAbstractSocket::ConnectedState);
		client.write("GET /overlay HTTP/1.1\r\nHost: x\r\n\r\n");
		QTRY_VERIFY((page += client.readAll()).contains("parade"));
		QVERIFY(page.contains("Cache-Control: no-store"));

		const auto fetch = [&server](const QByteArray &path) {
			QTcpSocket s;
			s.connectToHost(QStringLiteral("127.0.0.1"), server.port());
			QByteArray all;
			if (!QTest::qWaitFor([&s]() { return s.state() == QAbstractSocket::ConnectedState; }, 2000))
				return all;
			s.write("GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n");
			const bool closed = QTest::qWaitFor(
				[&]() {
					all += s.readAll();
					return s.state() != QAbstractSocket::ConnectedState;
				},
				2000);
			Q_UNUSED(closed);
			all += s.readAll();
			return all;
		};
		QVERIFY(fetch("/audios/10/som%20legal.mp3").contains("MP3DATA"));
		QVERIFY(fetch("/audios/10/som%20legal.mp3").contains("audio/mpeg"));
		QVERIFY(fetch("/static/assets/a.json").startsWith("HTTP/1.1 200"));
		QVERIFY(fetch("/static/../overlay.html").startsWith("HTTP/1.1 404"));
		QVERIFY(fetch("/audios/%2e%2e/secret").startsWith("HTTP/1.1 404"));
		QVERIFY(fetch("/tts/abc").endsWith("TTS"));
		QVERIFY(fetch("/tts/nope").startsWith("HTTP/1.1 404"));

		/* WebSocket: handshake, snapshot on connect, then broadcasts. */
		QCOMPARE(OverlayServer::acceptKey("dGhlIHNhbXBsZSBub25jZQ=="),
			 QByteArray("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
		QTcpSocket ws;
		ws.connectToHost(QStringLiteral("127.0.0.1"), server.port());
		QTRY_VERIFY(ws.state() == QAbstractSocket::ConnectedState);
		ws.write("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			 "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
		QByteArray got;
		QTRY_VERIFY((got += ws.readAll()).contains("\"snapshot\""));
		QVERIFY(got.startsWith("HTTP/1.1 101"));
		QVERIFY(got.contains("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
		QTRY_COMPARE(server.clientCount(), 1);
		server.broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("audio_stop")}});
		QTRY_VERIFY((got += ws.readAll()).contains("audio_stop"));
		ws.abort();
		QTRY_COMPARE(server.clientCount(), 0);
	}
};

QTEST_GUILESS_MAIN(TestTexuguito)
#include "test-texuguito.moc"
