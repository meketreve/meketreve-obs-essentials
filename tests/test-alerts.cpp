/*
Meketreve OBS Essentials - alerts unit tests (developer tool)
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
#include "chat-overlay.hpp"
#include "overlay-server.hpp"

#include <QJsonArray>
#include <QSet>
#include <QTcpSocket>
#include <QTest>

using namespace Alerts;

namespace {

QString key(const char *k)
{
	return QString::fromLatin1(k);
}

ChatMessage chat(ChatPlatform platform, ChatEvent event, int amount = 0, const QString &detail = QString(),
		 const QString &text = QString())
{
	ChatMessage msg{platform, QStringLiteral("Fulano"), QString(), text, QString()};
	msg.event = event;
	msg.amount = amount;
	msg.detail = detail;
	return msg;
}

QJsonObject typeOf(const QJsonObject &config, const char *type)
{
	return config.value(QStringLiteral("types")).toObject().value(QLatin1String(type)).toObject();
}

QJsonObject withType(QJsonObject config, const char *type, const QJsonObject &changes)
{
	QJsonObject types = config.value(QStringLiteral("types")).toObject();
	QJsonObject t = types.value(QLatin1String(type)).toObject();
	for (auto it = changes.begin(); it != changes.end(); ++it)
		t.insert(it.key(), it.value());
	types.insert(QLatin1String(type), t);
	config.insert(QStringLiteral("types"), types);
	return config;
}

} // namespace

class TestAlerts : public QObject {
	Q_OBJECT

private slots:
	void eventsBecomeAlerts()
	{
		QVERIFY(fromChat(chat(ChatPlatform::Twitch, ChatEvent::None)).type.isEmpty());
		QCOMPARE(fromChat(chat(ChatPlatform::Twitch, ChatEvent::Follow)).type, key("follow"));
		QCOMPARE(fromChat(chat(ChatPlatform::Twitch, ChatEvent::Sub, 1)).type, key("sub"));

		const Event resub =
			fromChat(chat(ChatPlatform::Kick, ChatEvent::Sub, 12, QString(), QStringLiteral("oi")));
		QCOMPARE(resub.type, key("resub"));
		QCOMPARE(resub.amount, key("12"));
		QCOMPARE(resub.value, 12.0);
		QCOMPARE(resub.message, key("oi"));
		QCOMPARE(resub.platform, key("kick"));

		const Event superChat =
			fromChat(chat(ChatPlatform::YouTube, ChatEvent::Donation, 0, QStringLiteral("R$ 10,00")));
		QCOMPARE(superChat.type, key("donation"));
		QCOMPARE(superChat.amount, key("R$ 10,00"));
		QCOMPARE(superChat.value, 10.0);

		const Event gift = fromChat(chat(ChatPlatform::Kick, ChatEvent::Gift, 100, QStringLiteral("Hype")));
		QCOMPARE(gift.type, key("gift"));
		QCOMPARE(gift.platform, key("kick"));
		QCOMPARE(gift.amount, key("100"));
		QCOMPARE(gift.detail, key("Hype"));
		QCOMPARE(gift.value, 100.0);

		QCOMPARE(fromChat(chat(ChatPlatform::Twitch, ChatEvent::Raid, 42)).value, 42.0);
		QCOMPARE(fromChat(chat(ChatPlatform::Kick, ChatEvent::Raid, 0)).amount, key("0"));
		QCOMPARE(fromChat(chat(ChatPlatform::Twitch, ChatEvent::Bits, 500)).type, key("bits"));
		QCOMPARE(fromChat(chat(ChatPlatform::YouTube, ChatEvent::Membership)).type, key("membership"));
		QCOMPARE(fromChat(chat(ChatPlatform::Twitch, ChatEvent::GiftSub, 5)).type, key("giftsub"));
		for (const QString &type : types())
			QCOMPARE(sample(type, key).type, type);
	}

	void money()
	{
		QCOMPARE(parseMoney(QStringLiteral("R$ 10,00")), 10.0);
		QCOMPARE(parseMoney(QStringLiteral("$5.00")), 5.0);
		QCOMPARE(parseMoney(QStringLiteral("€1.234,56")), 1234.56);
		QCOMPARE(parseMoney(QStringLiteral("$1,234.56")), 1234.56);
		QCOMPARE(parseMoney(QStringLiteral("¥500")), 500.0);
		QCOMPARE(parseMoney(QStringLiteral("R$ 1.000")), 1000.0);
		QCOMPARE(parseMoney(QStringLiteral("₹2,000")), 2000.0);
		QCOMPARE(parseMoney(QStringLiteral("CA$2.5")), 2.5);
		QCOMPARE(parseMoney(QString()), 0.0);
	}

	void configDefaultsAndCleaning()
	{
		const QJsonObject def = defaults(key);
		QCOMPARE(def.value(QStringLiteral("types")).toObject().size(), types().size());
		QCOMPARE(typeOf(def, "follow").value(QStringLiteral("text")).toString(), key("Alerts.Default.Follow"));

		/* Empty or broken input comes back as the defaults. */
		QCOMPARE(normalize(QJsonObject(), key), def);

		QJsonObject messy = withType(def, "bits",
					     {{QStringLiteral("duration"), 999},
					      {QStringLiteral("fontSize"), -3},
					      {QStringLiteral("animIn"), QStringLiteral("explode")},
					      {QStringLiteral("textColor"), QStringLiteral("red")},
					      {QStringLiteral("image"), QStringLiteral("javascript:alert(1)")},
					      {QStringLiteral("sound"), QStringLiteral("media:../../etc/passwd")},
					      {QStringLiteral("extra"), 1}});
		messy.insert(QStringLiteral("position"), QStringLiteral("nowhere"));
		messy.insert(QStringLiteral("junk"), true);
		QJsonObject messyTypes = messy.value(QStringLiteral("types")).toObject();
		messyTypes.insert(QStringLiteral("like"), QJsonObject{{QStringLiteral("enabled"), true}});
		messy.insert(QStringLiteral("types"), messyTypes);
		const QJsonObject clean = normalize(messy, key);
		const QJsonObject bits = typeOf(clean, "bits");
		QCOMPARE(bits.value(QStringLiteral("duration")).toDouble(), 60.0);
		QCOMPARE(bits.value(QStringLiteral("fontSize")).toDouble(), 12.0);
		QCOMPARE(bits.value(QStringLiteral("animIn")).toString(), key("bounce"));
		QCOMPARE(bits.value(QStringLiteral("textColor")).toString(), key("#FFFFFF"));
		QCOMPARE(bits.value(QStringLiteral("image")).toString(), QString());
		QCOMPARE(bits.value(QStringLiteral("sound")).toString(), QString());
		QVERIFY(!bits.contains(QStringLiteral("extra")));
		QCOMPARE(clean.value(QStringLiteral("position")).toString(), key("top-center"));
		QVERIFY(!clean.contains(QStringLiteral("junk")));
		QVERIFY(!clean.value(QStringLiteral("types")).toObject().contains(QStringLiteral("like")));

		/* API keys never reach the overlay. */
		QJsonObject keyed = def;
		keyed.insert(QStringLiteral("integrations"),
			     QJsonObject{{QStringLiteral("giphyKey"), QStringLiteral("abc")}});
		QCOMPARE(normalize(keyed, key)
				 .value(QStringLiteral("integrations"))
				 .toObject()
				 .value(QStringLiteral("giphyKey"))
				 .toString(),
			 key("abc"));
		QVERIFY(!forOverlay(normalize(keyed, key)).contains(QStringLiteral("integrations")));
	}

	void references()
	{
		QCOMPARE(cleanReference(QStringLiteral("builtin:star")), key("builtin:star"));
		QCOMPARE(cleanReference(QStringLiteral("media:hype.gif")), key("media:hype.gif"));
		QCOMPARE(cleanReference(QStringLiteral("media:../x.gif")), QString());
		QCOMPARE(cleanReference(QStringLiteral("https://media.giphy.com/media/abc/giphy.gif")),
			 key("https://media.giphy.com/media/abc/giphy.gif"));
		QCOMPARE(cleanReference(QStringLiteral("https://x.com/a\".gif")), QString());
		QCOMPARE(cleanReference(QStringLiteral("file:///etc/passwd")), QString());

		QCOMPARE(safeMediaName(QStringLiteral("Meu GIF (1).GIF")), key("Meu-GIF-1.gif"));
		QCOMPARE(safeMediaName(QStringLiteral("../../evil.mp3")), key("evil.mp3"));
		QCOMPARE(safeMediaName(QStringLiteral("C:\\Users\\x\\som.wav")), key("som.wav"));
		QCOMPARE(safeMediaName(QStringLiteral(".gif")), key("file.gif"));
		QCOMPARE(safeMediaName(QStringLiteral("page.html")), QString());
		QCOMPARE(safeMediaName(QStringLiteral("drawing.svg")), QString());
		QCOMPARE(mediaKind(QStringLiteral("a.webm")), key("video"));
		QCOMPARE(mediaKind(QStringLiteral("a.ogg")), key("audio"));
	}

	void minimumFilter()
	{
		QJsonObject config = defaults(key);
		const Event bits = fromChat(chat(ChatPlatform::Twitch, ChatEvent::Bits, 100));
		QVERIFY(passes(config, bits));
		config = withType(config, "bits", {{QStringLiteral("min"), 500}});
		QVERIFY(!passes(config, bits));
		QVERIFY(passes(config, fromChat(chat(ChatPlatform::Twitch, ChatEvent::Bits, 500))));
		config = withType(config, "bits", {{QStringLiteral("enabled"), false}});
		QVERIFY(!passes(config, fromChat(chat(ChatPlatform::Twitch, ChatEvent::Bits, 5000))));

		config = withType(config, "donation", {{QStringLiteral("min"), 5}});
		QVERIFY(!passes(config, fromChat(chat(ChatPlatform::YouTube, ChatEvent::Donation, 0, "R$ 2,00"))));
		QVERIFY(passes(config, fromChat(chat(ChatPlatform::YouTube, ChatEvent::Donation, 0, "R$ 5,00"))));
		QVERIFY(!passes(config, Event()));
	}

	void giftBundlesShowOnce()
	{
		GiftDedup dedup;
		const Event bundle = fromChat(chat(ChatPlatform::Twitch, ChatEvent::GiftSub, 3, "Tier 1"));
		const Event single = fromChat(chat(ChatPlatform::Twitch, ChatEvent::GiftSub, 1, "Ciclano"));
		QVERIFY(!dedup.swallow(bundle, 1000));
		QVERIFY(dedup.swallow(single, 1100));
		QVERIFY(dedup.swallow(single, 1200));
		QVERIFY(dedup.swallow(single, 1300));
		/* A fourth one is a new, separate gift. */
		QVERIFY(!dedup.swallow(single, 1400));
		/* Stale bundles are forgotten. */
		QVERIFY(!dedup.swallow(bundle, 2000));
		QVERIFY(!dedup.swallow(single, 2000 + 61000));
		QVERIFY(!dedup.swallow(fromChat(chat(ChatPlatform::Twitch, ChatEvent::Follow)), 0));
	}

	void localOnly()
	{
		QVERIFY(isLocalHost("localhost:8902", 8902));
		QVERIFY(isLocalHost("127.0.0.1:8902", 8902));
		QVERIFY(!isLocalHost("evil.example:8902", 8902));
		QVERIFY(!isLocalHost("localhost:8901", 8902));
		QVERIFY(isLocalOrigin("", 8902));
		QVERIFY(isLocalOrigin("http://localhost:8902", 8902));
		QVERIFY(!isLocalOrigin("https://evil.example", 8902));
		QVERIFY(!isLocalOrigin("null", 8902));
	}

	void serverTakesPostBodies()
	{
		OverlayServer::Routes routes;
		routes.maxBody = 1024;
		QByteArray seenBody;
		QString seenQuery;
		routes.handler = [&](const OverlayServer::Request &request, OverlayServer::Reply &reply) {
			if (request.path != QLatin1String("/api/echo"))
				return false;
			seenBody = request.body;
			seenQuery = request.query;
			reply.type = "text/plain";
			reply.body = request.method + ':' + request.headers.value("x-token") + ':' + request.body;
			return true;
		};
		OverlayServer server(routes);
		QVERIFY(server.listen(0));

		const auto send = [&server](const QByteArray &raw, bool slowly = false) {
			QTcpSocket s;
			s.connectToHost(QStringLiteral("127.0.0.1"), server.port());
			QByteArray all;
			if (!QTest::qWaitFor([&s]() { return s.state() == QAbstractSocket::ConnectedState; }, 2000))
				return all;
			if (slowly) {
				/* Body arriving in pieces. */
				const qsizetype half = raw.size() - 3;
				s.write(raw.left(half));
				s.flush();
				QTest::qWait(50);
				s.write(raw.mid(half));
			} else {
				s.write(raw);
			}
			const bool closed = QTest::qWaitFor(
				[&]() {
					all += s.readAll();
					return s.state() != QAbstractSocket::ConnectedState;
				},
				2000);
			Q_UNUSED(closed);
			return all + s.readAll();
		};

		QByteArray got = send("POST /api/echo?type=bits HTTP/1.1\r\nHost: x\r\nX-Token: abc\r\n"
				      "Content-Length: 11\r\n\r\nhello world",
				      true);
		QVERIFY(got.startsWith("HTTP/1.1 200"));
		QVERIFY(got.endsWith("POST:abc:hello world"));
		QCOMPARE(seenBody, QByteArray("hello world"));
		QCOMPARE(seenQuery, key("type=bits"));

		got = send("POST /api/echo HTTP/1.1\r\nHost: x\r\nContent-Length: 5000\r\n\r\n");
		QVERIFY(got.startsWith("HTTP/1.1 413"));
		got = send("POST /other HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhi");
		QVERIFY(got.startsWith("HTTP/1.1 405"));
		got = send("GET /api/echo HTTP/1.1\r\nHost: x\r\n\r\n");
		QVERIFY(got.endsWith("GET::"));
	}

	void chatOverlayConfig()
	{
		const QJsonObject d = ChatOverlay::defaults();
		const QJsonObject platforms = d.value(key("platforms")).toObject();
		QVERIFY(platforms.value(key("twitch")).toBool() && platforms.value(key("youtube")).toBool() &&
			platforms.value(key("kick")).toBool());
		QVERIFY(d.value(key("hideCommands")).toBool());
		QCOMPARE(d.value(key("newest")).toString(), key("bottom"));

		/* Platforms turned off stay off; junk is cleaned. */
		const QJsonObject n = ChatOverlay::normalize(
			QJsonObject{{key("platforms"), QJsonObject{{key("youtube"), false}, {key("kick"), false}}},
				    {key("fontSize"), 500},
				    {key("textColor"), key("red")},
				    {key("newest"), key("sideways")},
				    {key("hideUsers"), key("")}});
		QCOMPARE(n.value(key("platforms")).toObject().value(key("twitch")).toBool(), true);
		QCOMPARE(n.value(key("platforms")).toObject().value(key("youtube")).toBool(), false);
		QCOMPARE(n.value(key("fontSize")).toDouble(), 72.0);
		QCOMPARE(n.value(key("textColor")).toString(), key("#FFFFFF"));
		QCOMPARE(n.value(key("newest")).toString(), key("bottom"));
		QCOMPARE(n.value(key("hideUsers")).toString(), QString());
	}

	void chatOverlayFilter()
	{
		const QJsonObject config = ChatOverlay::defaults();
		ChatMessage msg = chat(ChatPlatform::Twitch, ChatEvent::None, 0, QString(), key("oi chat"));
		QVERIFY(ChatOverlay::passes(config, msg));

		ChatMessage command = msg;
		command.text = key("  !pontos");
		QVERIFY(!ChatOverlay::passes(config, command));
		QJsonObject showCommands = config;
		showCommands.insert(key("hideCommands"), false);
		QVERIFY(ChatOverlay::passes(showCommands, command));

		/* Bots by name, the YouTube @ and case do not matter. */
		ChatMessage bot = msg;
		bot.author = key("@NightBot");
		QVERIFY(!ChatOverlay::passes(config, bot));

		/* Events are for the alerts, empty lines for nobody. */
		QVERIFY(!ChatOverlay::passes(config,
					     chat(ChatPlatform::Twitch, ChatEvent::Sub, 1, QString(), key("oi"))));
		QVERIFY(!ChatOverlay::passes(config, chat(ChatPlatform::Kick, ChatEvent::None)));
	}

	void chatOverlayJson()
	{
		ChatMessage msg =
			chat(ChatPlatform::Kick, ChatEvent::None, 0, QString(), QString::fromUtf8("😀 oi KEKW"));
		msg.id = key("m1");
		msg.userId = key("u1");
		msg.isMod = true;
		/* UTF-16 positions, the same as JavaScript's: the emoji takes two. */
		msg.emotes.append(ChatEmote{6, 4, key("https://files.kick.com/emotes/1/fullsize")});
		msg.emotes.append(ChatEmote{8, 9, key("https://bad")});
		const QJsonObject j = ChatOverlay::toJson(msg);
		QCOMPARE(j.value(key("platform")).toString(), key("kick"));
		QCOMPARE(j.value(key("id")).toString(), key("m1"));
		QCOMPARE(j.value(key("user")).toString(), key("u1"));
		QVERIFY(j.value(key("mod")).toBool());
		const QJsonArray emotes = j.value(key("emotes")).toArray();
		QCOMPARE(emotes.size(), 1);
		QCOMPARE(msg.text.mid(emotes.at(0).toObject().value(key("start")).toInt(),
				      emotes.at(0).toObject().value(key("length")).toInt()),
			 key("KEKW"));

		/* Twitch emotes go animated on screen; other links stay as they are. */
		ChatMessage twitch = chat(ChatPlatform::Twitch, ChatEvent::None, 0, QString(), key("hi PogChamp"));
		twitch.emotes.append(
			ChatEmote{3, 8, key("https://static-cdn.jtvnw.net/emoticons/v2/emotesv2_abc/static/dark/2.0")});
		QCOMPARE(ChatOverlay::toJson(twitch)
				 .value(key("emotes"))
				 .toArray()
				 .at(0)
				 .toObject()
				 .value(key("url"))
				 .toString(),
			 key("https://static-cdn.jtvnw.net/emoticons/v2/emotesv2_abc/default/dark/2.0"));
		QCOMPARE(emotes.at(0).toObject().value(key("url")).toString(),
			 key("https://files.kick.com/emotes/1/fullsize"));

		/* The samples cover every platform. */
		QSet<QString> seen;
		for (const QJsonValue v : ChatOverlay::samples())
			seen.insert(v.toObject().value(key("platform")).toString());
		QCOMPARE(seen.size(), 3);
	}
};

QTEST_MAIN(TestAlerts)
#include "test-alerts.moc"
