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
#include "event-history.hpp"
#include "goals.hpp"
#include "poll.hpp"
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

	void eventsOverlay()
	{
		const TextLookup text = [](const char *k) {
			return QString::fromLatin1(k);
		};
		const QJsonObject d = EventsOverlay::defaults(text);
		QCOMPARE(d.value(key("labels")).toObject().value(key("ultimo-sub")).toString(),
			 key("EventsOverlay.Default.LastSub"));
		QVERIFY(d.value(key("list")).toObject().value(key("types")).toObject().value(key("raid")).toBool());
		QCOMPARE(EventsOverlay::normalize(QJsonObject{{key("list"), QJsonObject{{key("max"), 99}}}}, text)
				 .value(key("list"))
				 .toObject()
				 .value(key("max"))
				 .toDouble(),
			 20.0);

		EventsOverlay::History h;
		const auto add = [&h](ChatPlatform p, ChatEvent e, const char *who, int amount, const char *detail) {
			ChatMessage m = chat(p, e, amount, QString::fromUtf8(detail));
			m.author = QString::fromUtf8(who);
			h.add(EventsOverlay::entryFrom(fromChat(m), key("linha"), QString(), 1));
		};
		QVERIFY(h.labels().isEmpty());
		add(ChatPlatform::Twitch, ChatEvent::Sub, "Ana", 1, "1000");
		add(ChatPlatform::YouTube, ChatEvent::Donation, "@Rico", 0, "R$ 10,00");
		add(ChatPlatform::YouTube, ChatEvent::Donation, "@Rico", 0, "R$ 15,50");
		add(ChatPlatform::Kick, ChatEvent::Raid, "Vizinha", 42, "");
		add(ChatPlatform::YouTube, ChatEvent::Donation, "@Outra", 0, "R$ 20,00");
		add(ChatPlatform::Twitch, ChatEvent::Bits, "Bia", 300, "");
		add(ChatPlatform::Twitch, ChatEvent::Bits, "Bia", 200, "");

		QJsonObject labels = h.labels();
		QCOMPARE(labels.value(key("ultimo-sub")).toObject().value(key("name")).toString(), key("Ana"));
		QCOMPARE(labels.value(key("ultimo-raid")).toObject().value(key("amount")).toString(), key("42"));
		QCOMPARE(labels.value(key("ultima-doacao")).toObject().value(key("name")).toString(), key("@Outra"));
		/* @Rico's two Super Chats add up past @Outra's one. */
		const QJsonObject top = labels.value(key("top-doador")).toObject();
		QCOMPARE(top.value(key("name")).toString(), key("@Rico"));
		QVERIFY(top.value(key("amount")).toString().startsWith(key("R$ ")));
		QVERIFY(top.value(key("amount")).toString().contains(key("25")));
		QCOMPARE(labels.value(key("top-bits")).toObject().value(key("amount")).toString(), key("500"));
		QVERIFY(!labels.contains(key("ultimo-follow")));

		/* Newest first, and a restart keeps all of it. */
		QCOMPARE(h.recent(2).at(0).toObject().value(key("type")).toString(), key("bits"));
		EventsOverlay::History again;
		again.load(h.save());
		QCOMPARE(again.labels(), labels);

		/* A raid stays the last raid after the list fills with follows. */
		for (int i = 0; i < EventsOverlay::History::kKeep + 5; i++)
			add(ChatPlatform::Twitch, ChatEvent::Follow, "seguidor", 0, "");
		QCOMPARE(h.labels().value(key("ultimo-raid")).toObject().value(key("name")).toString(), key("Vizinha"));
		QCOMPARE(h.recent(500).size(), EventsOverlay::History::kKeep);

		/* A new live starts the tops over; the latest ones stay. */
		again.resetTop();
		QVERIFY(!again.labels().contains(key("top-doador")));
		QVERIFY(again.labels().contains(key("ultimo-sub")));
	}

	void chatOverlayEvents()
	{
		/* Bits come in as a chat line already; everything starts off. */
		QVERIFY(!ChatOverlay::eventTypes().contains(key("bits")));
		QVERIFY(ChatOverlay::eventTypes().contains(key("raid")));
		QJsonObject config = ChatOverlay::defaults();
		for (const QString &type : ChatOverlay::eventTypes())
			QVERIFY(!config.value(key("events")).toObject().value(type).toBool());

		const Event raid = fromChat(chat(ChatPlatform::Twitch, ChatEvent::Raid, 42));
		QCOMPARE(raid.type, key("raid"));
		QVERIFY(!ChatOverlay::passesEvent(config, raid));
		config = ChatOverlay::normalize(
			QJsonObject{{key("events"), QJsonObject{{key("raid"), true}, {key("bits"), true}}}});
		QVERIFY(ChatOverlay::passesEvent(config, raid));
		QVERIFY(!config.value(key("events")).toObject().contains(key("bits")));
		QVERIFY(!ChatOverlay::passesEvent(config, fromChat(chat(ChatPlatform::Twitch, ChatEvent::Bits, 500))));

		const QJsonObject j = ChatOverlay::eventToJson(raid, key("Fulano fez raid com 42"), key("e1"));
		QCOMPARE(j.value(key("platform")).toString(), key("twitch"));
		QCOMPARE(j.value(key("type")).toString(), key("raid"));
		QCOMPARE(j.value(key("text")).toString(), key("Fulano fez raid com 42"));
	}

	void goals()
	{
		const TextLookup text = [](const char *k) {
			return QString::fromLatin1(k);
		};
		/* A first start has one follows goal. */
		QJsonObject config = Goals::normalize(QJsonObject(), text);
		QJsonArray goals = config.value(key("goals")).toArray();
		QCOMPARE(goals.size(), 1);
		QCOMPARE(goals.at(0).toObject().value(key("kind")).toString(), key("follows"));
		QCOMPARE(goals.at(0).toObject().value(key("id")).toString(), key("meta-1"));
		/* An empty list stays empty; junk is cleaned. */
		QCOMPARE(Goals::normalize(QJsonObject{{key("goals"), QJsonArray()}}, text)
				 .value(key("goals"))
				 .toArray()
				 .size(),
			 0);
		const QJsonObject junk =
			Goals::normalize(QJsonObject{{key("goals"), QJsonArray{QJsonObject{{key("id"), key("Bad Id!")},
											   {key("kind"), key("likes")},
											   {key("target"), -5},
											   {key("current"), 1e12}}}},
						     {key("fontSize"), 500},
						     {key("accent"), key("red")}},
					 text);
		const QJsonObject cleaned = junk.value(key("goals")).toArray().at(0).toObject();
		QCOMPARE(cleaned.value(key("id")).toString(), key("meta-1"));
		QCOMPARE(cleaned.value(key("kind")).toString(), key("follows"));
		QCOMPARE(cleaned.value(key("target")).toDouble(), 1.0);
		QCOMPARE(cleaned.value(key("current")).toDouble(), 1e9);
		QCOMPARE(junk.value(key("fontSize")).toDouble(), 72.0);
		QCOMPARE(junk.value(key("accent")).toString(), key("#8B5CF6"));

		/* What each event adds. */
		const auto event = [](const char *type, double value) {
			Event e;
			e.type = QString::fromLatin1(type);
			e.value = value;
			return e;
		};
		QCOMPARE(Goals::amountFor(key("subs"), event("resub", 12)), 1.0);
		QCOMPARE(Goals::amountFor(key("subs"), event("giftsub", 5)), 5.0);
		QCOMPARE(Goals::amountFor(key("gifts"), event("giftsub", 5)), 5.0);
		QCOMPARE(Goals::amountFor(key("gifts"), event("gift", 1)), 1.0);
		QCOMPARE(Goals::amountFor(key("bits"), event("bits", 500)), 500.0);
		QCOMPARE(Goals::amountFor(key("donations"), event("donation", 10.5)), 10.5);
		QCOMPARE(Goals::amountFor(key("follows"), event("sub", 1)), 0.0);
		QCOMPARE(Goals::amountFor(key("members"), event("membership", 3)), 1.0);

		/* Events fill every goal of their kind; tests count nothing. */
		config = Goals::normalize(
			QJsonObject{{key("goals"), QJsonArray{QJsonObject{{key("id"), key("subs")},
									  {key("kind"), key("subs")},
									  {key("target"), 10},
									  {key("resetOnLive"), true}},
							      QJsonObject{{key("id"), key("money")},
									  {key("kind"), key("donations")},
									  {key("target"), 500}}}}},
			text);
		QVERIFY(Goals::apply(config, event("giftsub", 5)));
		QVERIFY(Goals::apply(config, event("donation", 10.256)));
		QVERIFY(!Goals::apply(config, event("follow", 1)));
		Event test = event("sub", 1);
		test.test = true;
		QVERIFY(!Goals::apply(config, test));
		const auto current = [](const QJsonObject &c, int i) {
			return c.value(QStringLiteral("goals"))
				.toArray()
				.at(i)
				.toObject()
				.value(QStringLiteral("current"))
				.toDouble();
		};
		QCOMPARE(current(config, 0), 5.0);
		QCOMPARE(current(config, 1), 10.26);

		/* The editor saves while events arrive: the counts stay. */
		QJsonObject edited = config;
		QJsonArray list = edited.value(key("goals")).toArray();
		QJsonObject first = list.at(0).toObject();
		first.insert(key("current"), 0);
		first.insert(key("title"), key("Subs!"));
		list.replace(0, first);
		/* A goal removed and a new one added: the new one starts at zero. */
		list.removeAt(1);
		list.append(QJsonObject{{key("kind"), key("bits")}, {key("current"), 0}});
		edited.insert(key("goals"), list);
		config = Goals::normalize(edited, text, config);
		QCOMPARE(current(config, 0), 5.0);
		QCOMPARE(config.value(key("goals")).toArray().at(0).toObject().value(key("title")).toString(),
			 key("Subs!"));
		QCOMPARE(current(config, 1), 0.0);

		/* Buttons: add, take away (never below zero), set. */
		QVERIFY(Goals::adjust(config, key("subs"), 2, false));
		QCOMPARE(current(config, 0), 7.0);
		QVERIFY(Goals::adjust(config, key("subs"), -20, false));
		QCOMPARE(current(config, 0), 0.0);
		QVERIFY(Goals::adjust(config, key("subs"), 3, true));
		QVERIFY(!Goals::adjust(config, key("nope"), 1, false));

		/* A new live resets only the goals marked for it. */
		const QString bitsId =
			config.value(key("goals")).toArray().at(1).toObject().value(key("id")).toString();
		QVERIFY(Goals::adjust(config, bitsId, 100, true));
		QVERIFY(Goals::resetForLive(config));
		QCOMPARE(current(config, 0), 0.0);
		QCOMPARE(current(config, 1), 100.0);
		QVERIFY(!Goals::resetForLive(config));
	}

	void poll()
	{
		QCOMPARE(Poll::voteFromChat(key("!voto 2")), 2);
		QCOMPARE(Poll::voteFromChat(key("  !VOTE 3 please")), 3);
		QCOMPARE(Poll::voteFromChat(key("!voto 12")), 0);
		QCOMPARE(Poll::voteFromChat(key("!votos 1")), 0);
		QCOMPARE(Poll::voteFromChat(key("eu !voto 1")), 0);

		Poll::Session p;
		QVERIFY(!p.exists());
		QVERIFY(p.toJson(0).isEmpty());
		QCOMPARE(p.start(key(""), {key("a"), key("b")}, 60, 0), key("question"));
		QCOMPARE(p.start(key("Q?"), {key("a"), key(" ")}, 60, 0), key("options"));
		QCOMPARE(p.start(key("Q?"), {key("a"), key("b")}, 99999, 0), key("duration"));
		QVERIFY(p.start(key("Qual jogo?"), {key("Celeste"), key("Hades"), key("Tetris")}, 60, 1000).isEmpty());
		QVERIFY(p.isOpen());

		/* One vote per person per platform; changing it moves the vote. */
		QVERIFY(p.vote(key("twitch:1"), 1, 2000));
		QVERIFY(p.vote(key("kick:1"), 2, 2000));
		QVERIFY(!p.vote(key("twitch:1"), 1, 2000));
		QVERIFY(p.vote(key("twitch:1"), 2, 2000));
		QVERIFY(!p.vote(key("youtube:x"), 4, 2000));
		QVERIFY(!p.vote(key("youtube:x"), 0, 2000));
		QCOMPARE(p.counts(), (QList<int>{0, 2, 0}));
		QCOMPARE(p.winner(), 2);
		const QJsonObject open = p.toJson(31000);
		QCOMPARE(open.value(key("total")).toInt(), 2);
		QCOMPARE(open.value(key("remainingMs")).toDouble(), 30000.0);

		/* Saved and loaded as it was. */
		Poll::Session copy;
		copy.load(p.save());
		QCOMPARE(copy.counts(), p.counts());
		QVERIFY(copy.isOpen());

		/* Time up: closed, and late votes do not count. */
		QVERIFY(!p.expire(60999));
		QVERIFY(p.expire(61000));
		QVERIFY(!p.isOpen());
		QVERIFY(!p.vote(key("youtube:y"), 1, 62000));
		QCOMPARE(p.toJson(62000).value(key("closedAt")).toDouble(), 61000.0);
		QVERIFY(!p.stop(63000));

		/* No time limit: open until closed by hand. Ties go to the first. */
		QVERIFY(p.start(key("Sim ou não?"), {key("Sim"), key("Não")}, 0, 0).isEmpty());
		QCOMPARE(p.winner(), 0);
		QVERIFY(!p.expire(99999999));
		QVERIFY(p.vote(key("a"), 2, 1));
		QVERIFY(p.vote(key("b"), 1, 1));
		QCOMPARE(p.winner(), 1);
		QVERIFY(p.stop(5));
		QVERIFY(!p.isOpen());

		/* Broken files load as no poll. */
		Poll::Session broken;
		broken.load(QJsonObject{{key("options"), QJsonArray{key("one")}}});
		QVERIFY(!broken.exists());

		const QJsonObject config = Poll::normalizeConfig(QJsonObject{{key("resultSeconds"), -3}});
		QCOMPARE(config.value(key("resultSeconds")).toDouble(), 0.0);
		QCOMPARE(config.value(key("announce")).toBool(), false);
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
