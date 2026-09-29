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
#pragma once

#include "chat-connector.hpp"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>

#include <functional>

class QJsonDocument;

class QNetworkAccessManager;

/* BetterTTV and 7TV emotes: the global sets and the ones of the Twitch
 * channel being read. They are plain words in chat; find() says which words
 * are emotes. */
class EmoteSets : public QObject {
	Q_OBJECT

public:
	explicit EmoteSets(QNetworkAccessManager *net, QObject *parent = nullptr);

	void loadGlobal();
	/* Loads the channel's sets when the Twitch room id changes. */
	void setTwitchChannel(const QString &roomId);
	/* Emotes among the words of text, leaving out words already covered. */
	QList<ChatEmote> find(const QString &text, const QList<ChatEmote> &taken = {}) const;
	qsizetype size() const;

	/* name -> picture URL. BTTV: [{id, code}]; 7TV: [{name, data: {host}}]. */
	static QHash<QString, QString> parseBttv(const QJsonArray &emotes);
	static QHash<QString, QString> parse7tv(const QJsonArray &emotes);

	/* For tests: set a list by hand (0 = channel 7TV, 1 = channel BTTV,
	 * 2 = global 7TV, 3 = global BTTV; earlier ones win). */
	void setSet(int index, const QHash<QString, QString> &emotes);

signals:
	void changed();

private:
	void fetch(const QString &url, std::function<void(const QJsonDocument &)> done);

	QNetworkAccessManager *m_net;
	QHash<QString, QString> m_sets[4];
	QString m_roomId;
};
