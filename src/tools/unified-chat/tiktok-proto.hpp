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

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

/* Just enough protobuf to walk TikTok's webcast messages. Field numbers come
 * from the tiktok-live-proto schema (WebcastPushFrame, ProtoMessageFetchResult,
 * WebcastChatMessage, User). */
struct PbField {
	quint32 number = 0;
	quint8 wire = 0;
	quint64 varint = 0;
	QByteArray bytes;
};

class PbReader {
public:
	explicit PbReader(const QByteArray &data) : m_data(data) {}

	bool next(PbField &f)
	{
		if (m_pos >= m_data.size())
			return false;
		quint64 key = 0;
		if (!readVarint(key))
			return false;
		f.number = static_cast<quint32>(key >> 3);
		f.wire = static_cast<quint8>(key & 7);
		f.bytes.clear();
		f.varint = 0;

		switch (f.wire) {
		case 0:
			return readVarint(f.varint);
		case 1:
			return skip(8);
		case 2: {
			quint64 len = 0;
			if (!readVarint(len) || len > static_cast<quint64>(m_data.size() - m_pos))
				return false;
			f.bytes = m_data.mid(m_pos, static_cast<qsizetype>(len));
			m_pos += static_cast<qsizetype>(len);
			return true;
		}
		case 5:
			return skip(4);
		default:
			return false;
		}
	}

private:
	bool readVarint(quint64 &out)
	{
		out = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (m_pos >= m_data.size())
				return false;
			const auto c = static_cast<quint8>(m_data[m_pos++]);
			out |= static_cast<quint64>(c & 0x7F) << shift;
			if (!(c & 0x80))
				return true;
		}
		return false;
	}

	bool skip(qsizetype n)
	{
		if (m_pos + n > m_data.size())
			return false;
		m_pos += n;
		return true;
	}

	const QByteArray &m_data;
	qsizetype m_pos = 0;
};

inline void putVarint(QByteArray &out, quint64 v)
{
	while (v >= 0x80) {
		out.append(static_cast<char>((v & 0x7F) | 0x80));
		v >>= 7;
	}
	out.append(static_cast<char>(v));
}

inline void putVarintField(QByteArray &out, quint32 field, quint64 v)
{
	putVarint(out, static_cast<quint64>(field) << 3);
	putVarint(out, v);
}

inline void putBytesField(QByteArray &out, quint32 field, const QByteArray &v)
{
	putVarint(out, (static_cast<quint64>(field) << 3) | 2);
	putVarint(out, static_cast<quint64>(v.size()));
	out += v;
}

inline QByteArray pushFrame(const QByteArray &type, const QByteArray &payload, quint64 logId = 0)
{
	QByteArray out;
	if (logId)
		putVarintField(out, 2, logId);
	putBytesField(out, 6, "pb");
	putBytesField(out, 7, type);
	putBytesField(out, 8, payload);
	return out;
}

struct FetchResult {
	QList<QPair<QByteArray, QByteArray>> messages;
	QList<QPair<QByteArray, QByteArray>> routeParams;
	QByteArray cursor;
	QByteArray internalExt;
	QByteArray pushServer;
	bool needAck = false;
};

FetchResult parseFetchResult(const QByteArray &data);

struct TikTokUser {
	QString nickname;
	QString uniqueId;

	QString displayName() const { return nickname.isEmpty() ? uniqueId : nickname; }
};

struct TikTokChatMessage {
	TikTokUser user;
	QString text;
};

/* WebcastGiftMessage: user=7, repeatCount=5, repeatEnd=9, gift=15
 * (Gift: type=11, diamondCount=12, name=16). */
struct TikTokGift {
	TikTokUser user;
	QString name;
	int repeatCount = 0;
	int diamonds = 0;
	bool streakable = false; /* gift type 1: sent as a combo */
	bool streakEnded = false;

	/* Streakable gifts arrive once per tap; only the last one counts. */
	bool isFinal() const { return !streakable || streakEnded; }
};

/* WebcastSocialMessage: user=2, common=1 (displayText=8 -> key=1). */
struct TikTokSocial {
	TikTokUser user;
	QString displayKey; /* contains "follow" or "share" */
};

/* WebcastLikeMessage: count=2, total=3, user=5. */
struct TikTokLike {
	TikTokUser user;
	int count = 0;
	qint64 total = 0;
};

TikTokUser parseTikTokUser(const QByteArray &data);
bool parseTikTokChat(const QByteArray &data, TikTokChatMessage &out);
bool parseTikTokGift(const QByteArray &data, TikTokGift &out);
bool parseTikTokSocial(const QByteArray &data, TikTokSocial &out);
bool parseTikTokLike(const QByteArray &data, TikTokLike &out);
