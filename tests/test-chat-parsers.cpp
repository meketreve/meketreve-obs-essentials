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

#include "irc-message.hpp"
#include "kick-chat.hpp"
#include "oauth-util.hpp"
#include "tiktok-proto.hpp"
#include "twitch-chat.hpp"
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

		const QList<ChatMessage> msgs = collect(spy);
		QCOMPARE(msgs.size(), 4);
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
	}

	void tiktokEvents()
	{
		QByteArray user;
		putBytesField(user, 3, "Fan");
		QByteArray giftInfo;
		putVarintField(giftInfo, 11, 1);
		putVarintField(giftInfo, 12, 5);
		putBytesField(giftInfo, 16, "Rose");
		QByteArray gift;
		putVarintField(gift, 5, 7);
		putBytesField(gift, 7, user);
		putBytesField(gift, 15, giftInfo);

		TikTokGift g;
		QVERIFY(parseTikTokGift(gift, g));
		QCOMPARE(g.name, QStringLiteral("Rose"));
		QCOMPARE(g.repeatCount, 7);
		QCOMPARE(g.diamonds, 5);
		QVERIFY(g.streakable);
		QVERIFY(!g.isFinal());
		putVarintField(gift, 9, 1);
		QVERIFY(parseTikTokGift(gift, g));
		QVERIFY(g.isFinal());

		QByteArray text;
		putBytesField(text, 1, "pm_main_follow_message_viewer_2");
		QByteArray common;
		putBytesField(common, 8, text);
		QByteArray social;
		putBytesField(social, 1, common);
		putBytesField(social, 2, user);
		TikTokSocial so;
		QVERIFY(parseTikTokSocial(social, so));
		QVERIFY(so.displayKey.contains(QStringLiteral("follow")));
		QCOMPARE(so.user.displayName(), QStringLiteral("Fan"));

		QByteArray like;
		putVarintField(like, 2, 15);
		putVarintField(like, 3, 99999);
		putBytesField(like, 5, user);
		TikTokLike l;
		QVERIFY(parseTikTokLike(like, l));
		QCOMPARE(l.count, 15);
		QCOMPARE(l.total, 99999);
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

	void protobufRoundTrip()
	{
		QByteArray user;
		putBytesField(user, 3, "Nick Name");
		putBytesField(user, 38, "nick_id");
		QByteArray chat;
		putVarintField(chat, 1, 300);
		putBytesField(chat, 2, user);
		putBytesField(chat, 3, "olá mundo");

		TikTokChatMessage msg;
		QVERIFY(parseTikTokChat(chat, msg));
		QCOMPARE(msg.user.nickname, QStringLiteral("Nick Name"));
		QCOMPARE(msg.user.uniqueId, QStringLiteral("nick_id"));
		QCOMPARE(msg.text, QStringLiteral("olá mundo"));
	}

	void protobufFetchResult()
	{
		QByteArray message;
		putBytesField(message, 1, "WebcastChatMessage");
		putBytesField(message, 2, "payload");
		QByteArray route;
		putBytesField(route, 1, "k");
		putBytesField(route, 2, "v");

		QByteArray fetch;
		putBytesField(fetch, 1, message);
		putBytesField(fetch, 2, "cursor-1");
		putBytesField(fetch, 5, "ext");
		putBytesField(fetch, 7, route);
		putVarintField(fetch, 9, 1);
		putBytesField(fetch, 10, "wss://push");

		const FetchResult r = parseFetchResult(fetch);
		QCOMPARE(r.messages.size(), 1);
		QCOMPARE(r.messages[0].first, QByteArray("WebcastChatMessage"));
		QCOMPARE(r.messages[0].second, QByteArray("payload"));
		QCOMPARE(r.cursor, QByteArray("cursor-1"));
		QCOMPARE(r.internalExt, QByteArray("ext"));
		QCOMPARE(r.routeParams.size(), 1);
		QVERIFY(r.needAck);
		QCOMPARE(r.pushServer, QByteArray("wss://push"));
	}

	void protobufTruncatedIsSafe()
	{
		QByteArray chat;
		putBytesField(chat, 3, "hello");
		chat.chop(2);
		TikTokChatMessage msg;
		parseTikTokChat(chat, msg);
		QVERIFY(msg.text.isEmpty());
		const FetchResult r = parseFetchResult(QByteArray("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 11));
		QVERIFY(r.messages.isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestChatParsers)
#include "test-chat-parsers.moc"
