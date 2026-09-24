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
#include "tiktok-proto.hpp"
#include "twitch-chat.hpp"

#include <QTest>

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
