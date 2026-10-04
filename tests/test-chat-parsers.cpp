/*
Meketreve OBS Essentials - chat parser unit tests (developer tool)
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

#include "chat-html.hpp"
#include "emote-sets.hpp"
#include "irc-message.hpp"
#include "kick-chat.hpp"
#include "oauth-util.hpp"
#include "twitch-chat.hpp"
#include "youtube-broadcast.hpp"
#include "youtube-chat.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

Q_DECLARE_METATYPE(ChatMessage)

namespace {

QList<ChatMessage> collect(QSignalSpy &spy)
{
	QList<ChatMessage> out;
	for (const QList<QVariant> &args : spy)
		out.append(args.at(0).value<ChatMessage>());
	return out;
}

QByteArray pusher(const char *event, const QJsonObject &data)
{
	return QJsonDocument(QJsonObject{{QStringLiteral("event"), QString::fromLatin1(event)},
					 {QStringLiteral("data"),
					  QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}})
		.toJson(QJsonDocument::Compact);
}

} // namespace

class TestChatParsers : public QObject {
	Q_OBJECT

private slots:
	void twitchAndKickEmotes()
	{
		/* Positions are code points: the emoji before Kappa is two UTF-16 units. */
		const QString text = QString::fromUtf8("\xF0\x9F\x98\x80 Kappa oi Kappa");
		const QList<ChatEmote> e = TwitchChat::parseEmotes(QStringLiteral("25:2-6,11-15"), text);
		QCOMPARE(e.size(), 2);
		QCOMPARE(text.mid(e[0].start, e[0].length), QStringLiteral("Kappa"));
		QCOMPARE(text.mid(e[1].start, e[1].length), QStringLiteral("Kappa"));
		QCOMPARE(e[0].url, QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/2.0"));
		QVERIFY(TwitchChat::parseEmotes(QStringLiteral("25:2-99"), text).isEmpty());
		QVERIFY(TwitchChat::parseEmotes(QString(), text).isEmpty());

		qRegisterMetaType<ChatMessage>();
		TwitchChat twitch(nullptr, nullptr);
		QSignalSpy spy(&twitch, &ChatConnector::messageReceived);
		twitch.handleLine("@emotes=25:0-4;room-id=71092938;display-name=Ana :ana!ana@ana.tmi.twitch.tv PRIVMSG "
				  "#xqc :Kappa hi");
		QCOMPARE(spy.size(), 1);
		const ChatMessage msg = spy[0][0].value<ChatMessage>();
		QCOMPARE(msg.channelId, QStringLiteral("71092938"));
		QCOMPARE(msg.emotes.size(), 1);

		QList<ChatEmote> kick;
		const QString kickText =
			KickChat::parseEmotes(QStringLiteral("oi [emote:37226:KEKW] e [emote:1:x]"), kick);
		QCOMPARE(kickText, QStringLiteral("oi KEKW e x"));
		QCOMPARE(kick.size(), 2);
		QCOMPARE(kickText.mid(kick[0].start, kick[0].length), QStringLiteral("KEKW"));
		QCOMPARE(kick[0].url, QStringLiteral("https://files.kick.com/emotes/37226/fullsize"));
		QCOMPARE(kickText.mid(kick[1].start, kick[1].length), QStringLiteral("x"));
	}

	void thirdPartyEmotes()
	{
		const QJsonArray bttv{QJsonObject{{QStringLiteral("id"), QStringLiteral("5590b223b344e2c42a9e28e3")},
						  {QStringLiteral("code"), QStringLiteral("monkaS")}},
				      QJsonObject{{QStringLiteral("id"), QStringLiteral("x")}}};
		const QHash<QString, QString> b = EmoteSets::parseBttv(bttv);
		QCOMPARE(b.size(), 1);
		QCOMPARE(b.value(QStringLiteral("monkaS")),
			 QStringLiteral("https://cdn.betterttv.net/emote/5590b223b344e2c42a9e28e3/2x"));

		const auto seven = [](const char *name, const char *id, QStringList files) {
			QJsonArray list;
			for (const QString &f : files)
				list.append(QJsonObject{{QStringLiteral("name"), f}});
			const QJsonObject host{{QStringLiteral("url"),
						QStringLiteral("//cdn.7tv.app/emote/") + QLatin1String(id)},
					       {QStringLiteral("files"), list}};
			return QJsonObject{{QStringLiteral("name"), QLatin1String(name)},
					   {QStringLiteral("data"), QJsonObject{{QStringLiteral("host"), host}}}};
		};
		const QJsonArray seventv{seven("Still", "A", {QStringLiteral("2x.webp"), QStringLiteral("2x.png")}),
					 seven("Moving", "B", {QStringLiteral("2x.webp"), QStringLiteral("2x.gif")}),
					 seven("Nothing", "C", {QStringLiteral("2x.avif")})};
		const QHash<QString, QString> s = EmoteSets::parse7tv(seventv);
		QCOMPARE(s.value(QStringLiteral("Still")), QStringLiteral("https://cdn.7tv.app/emote/A/2x.png"));
		QCOMPARE(s.value(QStringLiteral("Moving")), QStringLiteral("https://cdn.7tv.app/emote/B/2x.gif"));
		QVERIFY(!s.contains(QStringLiteral("Nothing")));

		EmoteSets sets(nullptr);
		sets.setSet(3, {{QStringLiteral("monkaS"), QStringLiteral("global-bttv")},
				{QStringLiteral("Kappa"), QStringLiteral("bttv-kappa")}});
		sets.setSet(0, {{QStringLiteral("monkaS"), QStringLiteral("channel-7tv")}});
		const QString text = QStringLiteral("Kappa monkaS monkaSS  monkaS");
		const QList<ChatEmote> taken{{0, 5, QStringLiteral("twitch-kappa")}};
		const QList<ChatEmote> found = sets.find(text, taken);
		QCOMPARE(found.size(), 3);
		QCOMPARE(found[0].url, QStringLiteral("twitch-kappa")); /* the platform's own wins */
		QCOMPARE(found[1].url, QStringLiteral("channel-7tv"));  /* channel beats global */
		QCOMPARE(text.mid(found[2].start, found[2].length), QStringLiteral("monkaS"));
		QCOMPARE(found[2].start, 22);
	}

	void linksAndEmotesInHtml()
	{
		QCOMPARE(linkTarget(QStringLiteral("www.site.com/a")), QStringLiteral("https://www.site.com/a"));
		QCOMPARE(linkTarget(QStringLiteral("javascript:alert(1)")), QString());
		QCOMPARE(linkTarget(QStringLiteral("https://")), QString());

		const QString html = chatHtml(QStringLiteral("veja https://x.com/a?b=1&c=2. e (www.y.com)"));
		QVERIFY(html.contains(QStringLiteral("<a href=\"https://x.com/a?b=1&amp;c=2\"")));
		QVERIFY(html.contains(QStringLiteral(">https://x.com/a?b=1&amp;c=2</a>. e (")));
		QVERIFY(html.contains(QStringLiteral("<a href=\"https://www.y.com\"")));
		QVERIFY(html.endsWith(QStringLiteral("www.y.com</a>)")));
		QVERIFY(chatHtml(QStringLiteral("https://pt.wikipedia.org/wiki/Foo_(bar)"))
				.contains(QStringLiteral("Foo_(bar)</a>")));
		QVERIFY(!chatHtml(QStringLiteral("<script>")).contains(QStringLiteral("<script")));

		const QString withEmote =
			chatHtml(QStringLiteral("oi Kappa <b>"), {{3, 5, QStringLiteral("https://e/1\"x")}}, 24);
		QCOMPARE(withEmote, QStringLiteral("oi <img src=\"https://e/1&quot;x\" height=\"24\" alt=\"Kappa\" "
						   "style=\"vertical-align:middle\"> &lt;b&gt;"));
		/* Broken ranges are ignored instead of cutting the text. */
		QCOMPARE(chatHtml(QStringLiteral("ab"), {{1, 9, QStringLiteral("https://e/1")}}), QStringLiteral("ab"));
	}

	void emojiGetsTheColorFont()
	{
		const QString open =
			QStringLiteral("<span style=\"font-family:'Noto Color Emoji','Segoe UI Emoji','Apple "
				       "Color Emoji'\">");
		const QString close = QStringLiteral("</span>");
		QCOMPARE(chatHtml(QStringLiteral("a <b> & c")), QStringLiteral("a &lt;b&gt; &amp; c"));
		/* 😀 */
		QCOMPARE(chatHtml(QString::fromUtf8("oi \xF0\x9F\x98\x80!")),
			 QStringLiteral("oi ") + open + QString::fromUtf8("\xF0\x9F\x98\x80") + close +
				 QStringLiteral("!"));
		/* 👍🏽 (skin tone), 🇧🇷 (flag) and 👨‍👩‍👧 (ZWJ family) stay one piece each. */
		for (const char *emoji : {"\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", "\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7",
					  "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"})
			QCOMPARE(chatHtml(QString::fromUtf8(emoji)), open + QString::fromUtf8(emoji) + close);
		/* Two flags in a row are two pieces. */
		const QString br = QString::fromUtf8("\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7");
		QCOMPARE(chatHtml(br + br), open + br + close + open + br + close);
		/* ❤️ and keycap 1️⃣; a plain © stays text. */
		QCOMPARE(chatHtml(QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F")),
			 open + QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F") + close);
		QCOMPARE(chatHtml(QString::fromUtf8("1\xEF\xB8\x8F\xE2\x83\xA3")),
			 open + QString::fromUtf8("1\xEF\xB8\x8F\xE2\x83\xA3") + close);
		QCOMPARE(chatHtml(QString::fromUtf8("\xC2\xA9 2026")), QString::fromUtf8("\xC2\xA9 2026"));
		QVERIFY(isEmojiCodePoint(0x1F525));
		QVERIFY(!isEmojiCodePoint(U'a'));
	}

	void ircPrivmsgWithTags()
	{
		IrcMessage m;
		QVERIFY(parseIrcLine("@badge-info=;color=#FF4500;display-name=Some\\sUser;id=abc-123;user-id=42 "
				     ":someuser!someuser@someuser.tmi.twitch.tv PRIVMSG #xqc :hello :) world",
				     m));
		QCOMPARE(m.command, QByteArray("PRIVMSG"));
		QCOMPARE(m.tag("display-name"), QStringLiteral("Some User"));
		QCOMPARE(m.tags.value("color"), QByteArray("#FF4500"));
		QCOMPARE(m.tags.value("id"), QByteArray("abc-123"));
		QCOMPARE(m.nick(), QByteArray("someuser"));
		QCOMPARE(m.trailing(), QByteArray("hello :) world"));
		QCOMPARE(m.tags.value("badge-info"), QByteArray());
	}

	void ircPing()
	{
		IrcMessage m;
		QVERIFY(parseIrcLine("PING :tmi.twitch.tv", m));
		QCOMPARE(m.command, QByteArray("PING"));
		QCOMPARE(m.params, QByteArray(":tmi.twitch.tv"));
		QCOMPARE(m.trailing(), QByteArray("tmi.twitch.tv"));
		QVERIFY(m.prefix.isEmpty());
	}

	void ircNumericNoTrailing()
	{
		IrcMessage m;
		QVERIFY(parseIrcLine(":tmi.twitch.tv 366 justinfan1 #xqc :End of /NAMES list", m));
		QCOMPARE(m.command, QByteArray("366"));
		QCOMPARE(m.nick(), QByteArray("tmi.twitch.tv"));
	}

	void ircEscapes()
	{
		QCOMPARE(unescapeIrcTag("a\\sb\\:c\\\\d\\r\\ne"), QStringLiteral("a b;c\\de"));
		QCOMPARE(unescapeIrcTag("trailing\\"), QStringLiteral("trailing\\"));
	}

	void ircRejectsGarbage()
	{
		IrcMessage m;
		QVERIFY(!parseIrcLine("@only-tags", m));
		QVERIFY(!parseIrcLine(":prefixonly", m));
		QVERIFY(!parseIrcLine("", m));
	}

	void twitchChannelNormalize()
	{
		QCOMPARE(TwitchChat::normalizeChannel(QStringLiteral("https://www.twitch.tv/XQC")),
			 QStringLiteral("xqc"));
		QCOMPARE(TwitchChat::normalizeChannel(QStringLiteral("#Foo")), QStringLiteral("foo"));
	}

	void twitchEvents()
	{
		qRegisterMetaType<ChatMessage>();
		TwitchChat chat(nullptr, nullptr);
		QSignalSpy spy(&chat, &ChatConnector::messageReceived);

		chat.handleLine("@badge-info=subscriber/5;color=#0000FF;display-name=Resubber;id=m1;"
				"msg-id=resub;msg-param-cumulative-months=5;msg-param-sub-plan=1000;user-id=11 "
				":tmi.twitch.tv USERNOTICE #xqc :five months!");
		chat.handleLine("@display-name=Gifter;msg-id=subgift;msg-param-recipient-display-name=Lucky;"
				"msg-param-sub-plan=1000 :tmi.twitch.tv USERNOTICE #xqc");
		chat.handleLine("@display-name=Santa;msg-id=submysterygift;msg-param-mass-gift-count=20;"
				"msg-param-sub-plan=2000 :tmi.twitch.tv USERNOTICE #xqc");
		chat.handleLine("@display-name=raider;msg-id=raid;msg-param-displayName=Raider;"
				"msg-param-viewerCount=1234 :tmi.twitch.tv USERNOTICE #xqc");
		chat.handleLine("@display-name=x;msg-id=announcement :tmi.twitch.tv USERNOTICE #xqc :hi");
		chat.handleLine(
			"@bits=100;display-name=Cheerer;id=m2;user-id=22 :cheerer!c@c PRIVMSG #xqc :Cheer100 gg");

		const QList<ChatMessage> msgs = collect(spy);
		QCOMPARE(msgs.size(), 5);
		QCOMPARE(msgs[0].event, ChatEvent::Sub);
		QCOMPARE(msgs[0].amount, 5);
		QCOMPARE(msgs[0].detail, QStringLiteral("Tier 1"));
		QCOMPARE(msgs[0].text, QStringLiteral("five months!"));
		QCOMPARE(msgs[0].id, QStringLiteral("m1"));
		QCOMPARE(msgs[1].event, ChatEvent::GiftSub);
		QCOMPARE(msgs[1].detail, QStringLiteral("Lucky"));
		QCOMPARE(msgs[2].amount, 20);
		QCOMPARE(msgs[2].detail, QStringLiteral("Tier 2"));
		QCOMPARE(msgs[3].event, ChatEvent::Raid);
		QCOMPARE(msgs[3].author, QStringLiteral("Raider"));
		QCOMPARE(msgs[3].amount, 1234);
		QCOMPARE(msgs[4].event, ChatEvent::Bits);
		QCOMPARE(msgs[4].amount, 100);
		QCOMPARE(msgs[4].userId, QStringLiteral("22"));
	}

	void kickEvents()
	{
		qRegisterMetaType<ChatMessage>();
		KickChat chat(nullptr, nullptr);
		QSignalSpy spy(&chat, &ChatConnector::messageReceived);

		chat.handleEvent(pusher("App\\Events\\ChatMessageEvent",
					QJsonObject{{QStringLiteral("id"), QStringLiteral("abc")},
						    {QStringLiteral("content"), QStringLiteral("hi [emote:1:KEKW]")},
						    {QStringLiteral("sender"), QJsonObject{{QStringLiteral("id"), 42},
											   {QStringLiteral("username"),
											    QStringLiteral("bob")}}}}));
		chat.handleEvent(pusher("App\\Events\\SubscriptionEvent",
					QJsonObject{{QStringLiteral("username"), QStringLiteral("sub")},
						    {QStringLiteral("months"), 3}}));
		chat.handleEvent(pusher("App\\Events\\GiftedSubscriptionsEvent",
					QJsonObject{{QStringLiteral("gifter_username"), QStringLiteral("g")},
						    {QStringLiteral("gifted_usernames"),
						     QJsonArray{QStringLiteral("a"), QStringLiteral("b")}}}));
		chat.handleEvent(pusher("App\\Events\\StreamHostEvent",
					QJsonObject{{QStringLiteral("host_username"), QStringLiteral("h")},
						    {QStringLiteral("number_viewers"), 50}}));
		chat.handleEvent(pusher("App\\Events\\FollowersUpdated",
					QJsonObject{{QStringLiteral("username"), QStringLiteral("f")},
						    {QStringLiteral("followed"), true}}));
		chat.handleEvent(pusher("App\\Events\\FollowersUpdated",
					QJsonObject{{QStringLiteral("username"), QStringLiteral("u")},
						    {QStringLiteral("followed"), false}}));

		const QList<ChatMessage> msgs = collect(spy);
		QCOMPARE(msgs.size(), 5);
		QCOMPARE(msgs[0].text, QStringLiteral("hi KEKW"));
		QCOMPARE(msgs[0].userId, QStringLiteral("42"));
		QCOMPARE(msgs[0].id, QStringLiteral("abc"));
		QCOMPARE(msgs[1].event, ChatEvent::Sub);
		QCOMPARE(msgs[1].amount, 3);
		QCOMPARE(msgs[2].event, ChatEvent::GiftSub);
		QCOMPARE(msgs[2].amount, 2);
		QCOMPARE(msgs[3].event, ChatEvent::Raid);
		QCOMPARE(msgs[3].amount, 50);
		QCOMPARE(msgs[4].event, ChatEvent::Follow);
		QCOMPARE(msgs[4].author, QStringLiteral("f"));
	}

	void youtubeEvents()
	{
		qRegisterMetaType<ChatMessage>();
		YouTubeChat chat(nullptr, nullptr);
		QSignalSpy spy(&chat, &ChatConnector::messageReceived);

		const auto item = [](const char *renderer, const char *json) {
			const QJsonObject r = QJsonDocument::fromJson(json).object();
			return QJsonObject{{QStringLiteral("addChatItemAction"),
					    QJsonObject{{QStringLiteral("item"),
							 QJsonObject{{QString::fromLatin1(renderer), r}}}}}};
		};
		chat.handleAction(item("liveChatPaidMessageRenderer",
				       R"({"id":"p1","authorExternalChannelId":"UC1","authorName":{"simpleText":"Rich"},
				       "purchaseAmountText":{"simpleText":"R$ 10,00"},"message":{"runs":[{"text":"oi"}]}})"));
		chat.handleAction(
			item("liveChatPaidStickerRenderer",
			     R"({"authorName":{"simpleText":"Stick"},"purchaseAmountText":{"simpleText":"$2.00"}})"));
		chat.handleAction(item(
			"liveChatMembershipItemRenderer",
			R"({"authorName":{"simpleText":"Member"},"headerSubtext":{"runs":[{"text":"Welcome!"}]}})"));
		chat.handleAction(item(
			"liveChatSponsorshipsGiftPurchaseAnnouncementRenderer",
			R"({"id":"g1","header":{"liveChatSponsorshipsHeaderRenderer":{"authorName":{"simpleText":"Giver"},
			"primaryText":{"runs":[{"text":"Gifted "},{"text":"5"},{"text":" Channel memberships"}]}}}})"));
		/* A long link shows shortened; the real one is in the redirect. */
		const QJsonObject linkRun{
			{QStringLiteral("text"), QStringLiteral("myinstants.com/pt/instant/vin...")},
			{QStringLiteral("navigationEndpoint"),
			 QJsonObject{{QStringLiteral("urlEndpoint"),
				      QJsonObject{{QStringLiteral("url"),
						   QStringLiteral("https://www.youtube.com/redirect?event=live_chat&q="
								  "https%3A%2F%2Fwww.myinstants.com%2Fpt%2Finstant%2F"
								  "vine-boom-sound-70972%2F")}}}}}};
		const QJsonObject textMessage{
			{QStringLiteral("id"), QStringLiteral("t1")},
			{QStringLiteral("authorName"),
			 QJsonObject{{QStringLiteral("simpleText"), QStringLiteral("Mod")}}},
			{QStringLiteral("message"),
			 QJsonObject{
				 {QStringLiteral("runs"),
				  QJsonArray{QJsonObject{{QStringLiteral("text"), QStringLiteral("!addaudio ")}},
					     linkRun, QJsonObject{{QStringLiteral("text"), QStringLiteral(" 50")}}}}}}};
		chat.handleAction(QJsonObject{
			{QStringLiteral("addChatItemAction"),
			 QJsonObject{{QStringLiteral("item"),
				      QJsonObject{{QStringLiteral("liveChatTextMessageRenderer"), textMessage}}}}}});

		const QList<ChatMessage> msgs = collect(spy);
		QCOMPARE(msgs.size(), 5);
		QCOMPARE(msgs[0].event, ChatEvent::Donation);
		QCOMPARE(msgs[0].detail, QStringLiteral("R$ 10,00"));
		QCOMPARE(msgs[0].text, QStringLiteral("oi"));
		QCOMPARE(msgs[0].userId, QStringLiteral("UC1"));
		QCOMPARE(msgs[1].event, ChatEvent::Donation);
		QCOMPARE(msgs[2].event, ChatEvent::Membership);
		QCOMPARE(msgs[2].detail, QStringLiteral("Welcome!"));
		QCOMPARE(msgs[3].event, ChatEvent::GiftSub);
		QCOMPARE(msgs[3].author, QStringLiteral("Giver"));
		QCOMPARE(msgs[3].amount, 5);
		QCOMPARE(msgs[4].text,
			 QStringLiteral("!addaudio https://www.myinstants.com/pt/instant/vine-boom-sound-70972/ 50"));
	}

	void youtubeChannelIdFromPage()
	{
		QCOMPARE(
			YouTubeChat::channelIdFromPage(
				"<link rel=\"canonical\" href=\"https://www.youtube.com/channel/UCMBSKO1nn-uXBwn9Sy_reDw\">"),
			QStringLiteral("UCMBSKO1nn-uXBwn9Sy_reDw"));
		QVERIFY(YouTubeChat::channelIdFromPage(
				"<link rel=\"canonical\" href=\"https://www.youtube.com/watch?v=dQw4w9WgXcQ\">")
				.isEmpty());
	}

	void viewerCounts()
	{
		/* Kick: livestream is null while offline. */
		const QJsonObject live{
			{QStringLiteral("livestream"),
			 QJsonObject{{QStringLiteral("is_live"), true}, {QStringLiteral("viewer_count"), 80568}}}};
		QCOMPARE(KickChat::viewersFromChannel(live), 80568);
		QCOMPARE(KickChat::viewersFromChannel(QJsonObject{{QStringLiteral("livestream"), QJsonValue()}}), -1);
		QCOMPARE(KickChat::viewersFromChannel(QJsonObject()), -1);

		/* YouTube updated_metadata: the count rides in one of the actions. */
		const QJsonObject renderer{{QStringLiteral("originalViewCount"), QStringLiteral("3990")},
					   {QStringLiteral("isLive"), true}};
		const QJsonObject viewership{{QStringLiteral("viewCount"),
					      QJsonObject{{QStringLiteral("videoViewCountRenderer"), renderer}}}};
		const QJsonArray actions{QJsonObject{{QStringLiteral("updateTitleAction"), QJsonObject()}},
					 QJsonObject{{QStringLiteral("updateViewershipAction"), viewership}}};
		QCOMPARE(YouTubeChat::viewersFromMetadata(QJsonObject{{QStringLiteral("actions"), actions}}), 3990);
		QCOMPARE(YouTubeChat::viewersFromMetadata(QJsonObject()), -1);
	}

	void youtubeBroadcastChoices()
	{
		using namespace YouTubeBroadcast;
		const auto stream = [](const char *id, const char *key) {
			return QJsonObject{
				{QStringLiteral("id"), QString::fromLatin1(id)},
				{QStringLiteral("cdn"),
				 QJsonObject{{QStringLiteral("ingestionInfo"),
					      QJsonObject{{QStringLiteral("streamName"), QString::fromLatin1(key)}}}}}};
		};
		const QJsonObject streams{
			{QStringLiteral("items"), QJsonArray{stream("s1", "aaaa-bbbb"), stream("s2", "cccc-dddd")}}};
		QCOMPARE(streamIdForKey(streams, QStringLiteral(" cccc-dddd ")), QStringLiteral("s2"));
		QCOMPARE(streamIdForKey(streams, QStringLiteral("zzzz")), QString());
		QCOMPARE(streamIdForKey(streams, QString()), QString());

		const auto broadcast = [](const char *id, const char *bound, const char *life, const char *title,
					  const char *end, const char *privacy) {
			return QJsonObject{
				{QStringLiteral("id"), QString::fromLatin1(id)},
				{QStringLiteral("snippet"),
				 QJsonObject{{QStringLiteral("title"), QString::fromUtf8(title)},
					     {QStringLiteral("description"),
					      QStringLiteral("desc ") + QString::fromLatin1(id)},
					     {QStringLiteral("actualEndTime"), QString::fromLatin1(end)}}},
				{QStringLiteral("status"),
				 QJsonObject{{QStringLiteral("lifeCycleStatus"), QString::fromLatin1(life)},
					     {QStringLiteral("privacyStatus"), QString::fromLatin1(privacy)}}},
				{QStringLiteral("contentDetails"),
				 QJsonObject{{QStringLiteral("boundStreamId"), QString::fromLatin1(bound)}}}};
		};
		/* Two finished lives and one ready on another key: nothing to reuse for s2. */
		QJsonObject list{
			{QStringLiteral("items"),
			 QJsonArray{broadcast("b1", "s2", "complete", "Velha", "2026-10-01T02:00:00Z", "public"),
				    broadcast("b2", "s2", "complete", "Última live", "2026-10-03T23:00:00Z", "unlisted"),
				    broadcast("b3", "s1", "ready", "Outra chave", "", "public")}}};
		QCOMPARE(reusable(list, QStringLiteral("s2")), QString());
		QCOMPARE(reusable(list, QStringLiteral("s1")), QStringLiteral("b3"));
		QCOMPARE(lastFinished(list).value(QStringLiteral("id")).toString(), QStringLiteral("b2"));

		/* The new one copies the last live and starts and stops with the video. */
		const QDateTime now = QDateTime::fromString(QStringLiteral("2026-10-04T20:00:00Z"), Qt::ISODate);
		const QJsonObject body = newBroadcast(lastFinished(list), QStringLiteral("Canal · 04/10"), now);
		QCOMPARE(body.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Última live"));
		QCOMPARE(
			body.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("description")).toString(),
			QStringLiteral("desc b2"));
		QCOMPARE(body.value(QStringLiteral("status"))
				 .toObject()
				 .value(QStringLiteral("privacyStatus"))
				 .toString(),
			 QStringLiteral("unlisted"));
		QVERIFY(!body.value(QStringLiteral("status"))
				 .toObject()
				 .value(QStringLiteral("selfDeclaredMadeForKids"))
				 .toBool());
		QVERIFY(body.value(QStringLiteral("contentDetails"))
				.toObject()
				.value(QStringLiteral("enableAutoStart"))
				.toBool());
		QVERIFY(body.value(QStringLiteral("contentDetails"))
				.toObject()
				.value(QStringLiteral("enableAutoStop"))
				.toBool());
		QVERIFY(body.value(QStringLiteral("snippet"))
				.toObject()
				.value(QStringLiteral("scheduledStartTime"))
				.toString()
				.startsWith(QStringLiteral("2026-10-04T20:00:00")));

		/* First live ever: the fallback title, public. */
		const QJsonObject first = newBroadcast(QJsonObject(), QStringLiteral("Canal · 04/10"), now);
		QCOMPARE(first.value(QStringLiteral("snippet")).toObject().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Canal · 04/10"));
		QCOMPARE(first.value(QStringLiteral("status"))
				 .toObject()
				 .value(QStringLiteral("privacyStatus"))
				 .toString(),
			 QStringLiteral("public"));
	}

	void removalsFromPlatforms()
	{
		QList<ChatRemoval> got;
		const auto collect = [&got](ChatConnector &c) {
			QObject::connect(&c, &ChatConnector::removalReceived,
					 [&got](const ChatRemoval &r) { got.append(r); });
		};

		TwitchChat twitch(nullptr, nullptr);
		collect(twitch);
		twitch.handleLine("@login=troll;room-id=1;target-msg-id=abc-123;tmi-sent-ts=1 "
				  ":tmi.twitch.tv CLEARMSG #xqc :bad words");
		twitch.handleLine("@ban-duration=600;room-id=1;target-user-id=777;tmi-sent-ts=1 "
				  ":tmi.twitch.tv CLEARCHAT #xqc :troll");
		twitch.handleLine("@room-id=1;tmi-sent-ts=1 :tmi.twitch.tv CLEARCHAT #xqc");
		QCOMPARE(got.size(), 3);
		QCOMPARE(got[0].platform, ChatPlatform::Twitch);
		QCOMPARE(got[0].messageId, QStringLiteral("abc-123"));
		QCOMPARE(got[1].userId, QStringLiteral("777"));
		QVERIFY(!got[1].all);
		QVERIFY(got[2].all);

		got.clear();
		KickChat kick(nullptr, nullptr);
		collect(kick);
		const auto frame = [](const char *event, const QJsonObject &data) {
			return QJsonDocument(QJsonObject{{QStringLiteral("event"), QString::fromLatin1(event)},
							 {QStringLiteral("data"),
							  QString::fromUtf8(
								  QJsonDocument(data).toJson(QJsonDocument::Compact))}})
				.toJson(QJsonDocument::Compact);
		};
		kick.handleEvent(frame("App\\Events\\MessageDeletedEvent",
				       QJsonObject{{QStringLiteral("id"), QStringLiteral("x")},
						   {QStringLiteral("message"),
						    QJsonObject{{QStringLiteral("id"), QStringLiteral("m-9")}}}}));
		kick.handleEvent(
			frame("App\\Events\\UserBannedEvent",
			      QJsonObject{{QStringLiteral("user"),
					   QJsonObject{{QStringLiteral("id"), 57934691},
						       {QStringLiteral("username"), QStringLiteral("FArg2019")}}},
					  {QStringLiteral("permanent"), true}}));
		kick.handleEvent(frame("App\\Events\\ChatroomClearEvent",
				       QJsonObject{{QStringLiteral("id"), QStringLiteral("c")}}));
		QCOMPARE(got.size(), 3);
		QCOMPARE(got[0].platform, ChatPlatform::Kick);
		QCOMPARE(got[0].messageId, QStringLiteral("m-9"));
		QCOMPARE(got[1].userId, QStringLiteral("57934691"));
		QVERIFY(got[2].all);

		got.clear();
		YouTubeChat youtube(nullptr, nullptr);
		collect(youtube);
		const auto action = [](const char *name, const char *key, const char *value) {
			return QJsonObject{{QString::fromLatin1(name),
					    QJsonObject{{QString::fromLatin1(key), QString::fromLatin1(value)}}}};
		};
		youtube.handleAction(action("markChatItemAsDeletedAction", "targetItemId", "LCC.1"));
		youtube.handleAction(action("removeChatItemAction", "targetItemId", "LCC.2"));
		youtube.handleAction(action("markChatItemsByAuthorAsDeletedAction", "externalChannelId", "UCbad"));
		youtube.handleAction(action("removeChatItemByAuthorAction", "externalChannelId", "UCbad2"));
		QCOMPARE(got.size(), 4);
		QCOMPARE(got[0].platform, ChatPlatform::YouTube);
		QCOMPARE(got[1].messageId, QStringLiteral("LCC.2"));
		QCOMPARE(got[3].userId, QStringLiteral("UCbad2"));
	}

	void pkceMatchesRfc7636()
	{
		/* RFC 7636 appendix B. */
		QCOMPARE(OAuthUtil::codeChallengeS256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
			 QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
		const QByteArray v = OAuthUtil::newCodeVerifier();
		QVERIFY(v.size() >= 43 && v.size() <= 128);
		QVERIFY(!v.contains('+') && !v.contains('/') && !v.contains('='));
		QVERIFY(OAuthUtil::newState() != OAuthUtil::newState());
	}

	void callbackRequestLine()
	{
		QUrlQuery q;
		QCOMPARE(OAuthUtil::parseRequestLine("GET /callback?code=abc%20d&state=xyz HTTP/1.1\r\n", q),
			 QStringLiteral("/callback"));
		QCOMPARE(q.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("abc d"));
		QCOMPARE(q.queryItemValue(QStringLiteral("state")), QStringLiteral("xyz"));
		QVERIFY(OAuthUtil::parseRequestLine("POST /callback HTTP/1.1", q).isEmpty());
		QCOMPARE(OAuthUtil::formBody({{QStringLiteral("a b"), QStringLiteral("c&d")}}),
			 QByteArray("a%20b=c%26d"));
	}
};

QTEST_GUILESS_MAIN(TestChatParsers)
#include "test-chat-parsers.moc"
