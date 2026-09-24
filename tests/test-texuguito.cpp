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
		BotMessage m = msg(QStringLiteral("Ana"), QStringLiteral("@Beto !apelido Nova"));
		m.isReply = true;
		bot.handleMessage(m);
		QCOMPARE(bot.viewers().getOrCreate(QStringLiteral("ana")).nick, QStringLiteral("Nova"));
	}

	void pointsAndPrivileges()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
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
		QVERIFY(replies(said).last().startsWith(QStringLiteral("⏳ Cooldown ativo!")));

		bot.resetClipCooldown();
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QCOMPARE(replies(said).last(), QStringLiteral("🔊 Tocando: buzina alta. Saldo: 5 pts."));
		bot.resetClipCooldown();
		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!tocar buzina alta")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Pontos insuficientes! 'buzina alta' custa 10 pts."));

		bot.handleMessage(msg(QStringLiteral("ana"), QStringLiteral("!sons")));
		QCOMPARE(replies(said).last(), QStringLiteral("🎵 Sons Disponíveis: [10 pts: buzina alta]"));
	}

	void ttsChargesAndRefunds()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
		bot.setOverlayListeners(1);
		bot.points().add(QStringLiteral("ana"), 450);
		QString spoken;
		bool fail = false;
		bot.setTts([&](const QString &text, std::function<void(QByteArray, QString)> done) {
			spoken = text;
			done(fail ? QByteArray() : QByteArray("ID3mp3"), QString());
		});
		QSignalSpy said(&bot, &BotEngine::reply);
		QSignalSpy overlay(&bot, &BotEngine::overlayMessage);

		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!falar olá chat")));
		QCOMPARE(spoken, QStringLiteral("Ana enviou a mensagem: olá chat"));
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
		bot.handleMessage(msg(QStringLiteral("Ana"), QStringLiteral("!speak caro")));
		QCOMPARE(replies(said).last(), QStringLiteral("❌ Pontos insuficientes (200 pts necessários)."));
	}

	void customCommands()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
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

	void raffle()
	{
		QTemporaryDir dir;
		BotEngine bot(dir.path(), dir.filePath(QStringLiteral("audios")));
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
