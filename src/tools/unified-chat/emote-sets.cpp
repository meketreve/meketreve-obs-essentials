/*
Meketreve OBS Essentials - Unified Chat
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
#include "emote-sets.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <algorithm>

namespace {

enum Set { Channel7tv, ChannelBttv, Global7tv, GlobalBttv };

} // namespace

EmoteSets::EmoteSets(QNetworkAccessManager *net, QObject *parent) : QObject(parent), m_net(net) {}

QHash<QString, QString> EmoteSets::parseBttv(const QJsonArray &emotes)
{
	QHash<QString, QString> out;
	for (const QJsonValue v : emotes) {
		const QJsonObject e = v.toObject();
		const QString id = e.value(QStringLiteral("id")).toString();
		const QString code = e.value(QStringLiteral("code")).toString();
		if (!id.isEmpty() && !code.isEmpty())
			out.insert(code, QStringLiteral("https://cdn.betterttv.net/emote/%1/2x").arg(id));
	}
	return out;
}

QHash<QString, QString> EmoteSets::parse7tv(const QJsonArray &emotes)
{
	QHash<QString, QString> out;
	for (const QJsonValue v : emotes) {
		const QJsonObject e = v.toObject();
		const QString name = e.value(QStringLiteral("name")).toString();
		const QJsonObject host =
			e.value(QStringLiteral("data")).toObject().value(QStringLiteral("host")).toObject();
		const QString base = host.value(QStringLiteral("url")).toString();
		if (name.isEmpty() || base.isEmpty())
			continue;
		/* PNG for still emotes, GIF for animated ones: formats every
		 * Qt build reads (WebP and AVIF need extra plugins). */
		QStringList files;
		for (const QJsonValue f : host.value(QStringLiteral("files")).toArray())
			files.append(f.toObject().value(QStringLiteral("name")).toString());
		QString file;
		for (const char *pick : {"2x.png", "2x.gif", "1x.png", "1x.gif", "2x.webp"}) {
			if (files.contains(QLatin1String(pick))) {
				file = QLatin1String(pick);
				break;
			}
		}
		if (file.isEmpty())
			continue;
		out.insert(name, (base.startsWith(QLatin1String("//")) ? QStringLiteral("https:") : QString()) + base +
					 QLatin1Char('/') + file);
	}
	return out;
}

void EmoteSets::setSet(int index, const QHash<QString, QString> &emotes)
{
	if (index < 0 || index > 3)
		return;
	m_sets[index] = emotes;
	emit changed();
}

qsizetype EmoteSets::size() const
{
	qsizetype n = 0;
	for (const auto &set : m_sets)
		n += set.size();
	return n;
}

void EmoteSets::fetch(const QString &url, std::function<void(const QJsonDocument &)> done)
{
	QNetworkRequest request{QUrl(url)};
	request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowserUserAgent));
	QNetworkReply *reply = m_net->get(request);
	QPointer<EmoteSets> self = this;
	connect(reply, &QNetworkReply::finished, this, [self, reply, done]() {
		reply->deleteLater();
		if (!self || reply->error() != QNetworkReply::NoError)
			return;
		done(QJsonDocument::fromJson(reply->readAll()));
	});
}

void EmoteSets::loadGlobal()
{
	fetch(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global"),
	      [this](const QJsonDocument &doc) { setSet(GlobalBttv, parseBttv(doc.array())); });
	fetch(QStringLiteral("https://7tv.io/v3/emote-sets/global"), [this](const QJsonDocument &doc) {
		setSet(Global7tv, parse7tv(doc.object().value(QStringLiteral("emotes")).toArray()));
	});
}

void EmoteSets::setTwitchChannel(const QString &roomId)
{
	if (roomId.isEmpty() || roomId == m_roomId)
		return;
	m_roomId = roomId;
	m_sets[Channel7tv].clear();
	m_sets[ChannelBttv].clear();
	fetch(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/%1").arg(roomId),
	      [this, roomId](const QJsonDocument &doc) {
		      if (roomId != m_roomId)
			      return;
		      QHash<QString, QString> set =
			      parseBttv(doc.object().value(QStringLiteral("sharedEmotes")).toArray());
		      set.insert(parseBttv(doc.object().value(QStringLiteral("channelEmotes")).toArray()));
		      setSet(ChannelBttv, set);
	      });
	fetch(QStringLiteral("https://7tv.io/v3/users/twitch/%1").arg(roomId),
	      [this, roomId](const QJsonDocument &doc) {
		      if (roomId != m_roomId)
			      return;
		      setSet(Channel7tv, parse7tv(doc.object()
							  .value(QStringLiteral("emote_set"))
							  .toObject()
							  .value(QStringLiteral("emotes"))
							  .toArray()));
	      });
}

QList<ChatEmote> EmoteSets::find(const QString &text, const QList<ChatEmote> &taken) const
{
	QList<ChatEmote> out = taken;
	if (size() == 0)
		return out;
	qsizetype i = 0;
	while (i < text.size()) {
		while (i < text.size() && text[i].isSpace())
			i++;
		const qsizetype start = i;
		while (i < text.size() && !text[i].isSpace())
			i++;
		if (i == start)
			break;
		const bool covered = std::any_of(taken.begin(), taken.end(), [start, i](const ChatEmote &e) {
			return start < e.start + e.length && e.start < i;
		});
		if (covered)
			continue;
		const QString word = text.mid(start, i - start);
		for (const auto &set : m_sets) {
			const auto it = set.constFind(word);
			if (it != set.constEnd()) {
				out.append(ChatEmote{start, i - start, it.value()});
				break;
			}
		}
	}
	std::sort(out.begin(), out.end(), [](const ChatEmote &a, const ChatEmote &b) { return a.start < b.start; });
	return out;
}
