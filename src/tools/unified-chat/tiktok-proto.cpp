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

#include "tiktok-proto.hpp"

#include <algorithm>

FetchResult parseFetchResult(const QByteArray &data)
{
	FetchResult r;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		switch (f.number) {
		case 1: {
			QByteArray method, payload;
			PbReader msg(f.bytes);
			PbField m;
			while (msg.next(m)) {
				if (m.number == 1)
					method = m.bytes;
				else if (m.number == 2)
					payload = m.bytes;
			}
			r.messages.append({method, payload});
			break;
		}
		case 2:
			r.cursor = f.bytes;
			break;
		case 5:
			r.internalExt = f.bytes;
			break;
		case 7: {
			QByteArray key, value;
			PbReader entry(f.bytes);
			PbField e;
			while (entry.next(e)) {
				if (e.number == 1)
					key = e.bytes;
				else if (e.number == 2)
					value = e.bytes;
			}
			if (!value.isEmpty())
				r.routeParams.append({key, value});
			break;
		}
		case 9:
			r.needAck = f.varint != 0;
			break;
		case 10:
			r.pushServer = f.bytes;
			break;
		default:
			break;
		}
	}
	return r;
}

TikTokUser parseTikTokUser(const QByteArray &data)
{
	TikTokUser user;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		if (f.number == 3)
			user.nickname = QString::fromUtf8(f.bytes);
		else if (f.number == 38)
			user.uniqueId = QString::fromUtf8(f.bytes);
	}
	return user;
}

bool parseTikTokChat(const QByteArray &data, TikTokChatMessage &out)
{
	out = TikTokChatMessage();
	bool any = false;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		if (f.number == 2) {
			out.user = parseTikTokUser(f.bytes);
			any = true;
		} else if (f.number == 3) {
			out.text = QString::fromUtf8(f.bytes);
			any = true;
		}
	}
	return any;
}

bool parseTikTokGift(const QByteArray &data, TikTokGift &out)
{
	out = TikTokGift();
	bool any = false;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		switch (f.number) {
		case 5:
			out.repeatCount = static_cast<int>(f.varint);
			break;
		case 7:
			out.user = parseTikTokUser(f.bytes);
			any = true;
			break;
		case 9:
			out.streakEnded = f.varint != 0;
			break;
		case 15: {
			PbReader gift(f.bytes);
			PbField g;
			while (gift.next(g)) {
				if (g.number == 11)
					out.streakable = g.varint == 1;
				else if (g.number == 12)
					out.diamonds = static_cast<int>(g.varint);
				else if (g.number == 16)
					out.name = QString::fromUtf8(g.bytes);
			}
			break;
		}
		default:
			break;
		}
	}
	out.repeatCount = std::max(out.repeatCount, 1);
	return any;
}

bool parseTikTokSocial(const QByteArray &data, TikTokSocial &out)
{
	out = TikTokSocial();
	bool any = false;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		if (f.number == 2) {
			out.user = parseTikTokUser(f.bytes);
			any = true;
		} else if (f.number == 1) {
			PbReader common(f.bytes);
			PbField c;
			while (common.next(c)) {
				if (c.number != 8)
					continue;
				PbReader text(c.bytes);
				PbField t;
				while (text.next(t)) {
					if (t.number == 1)
						out.displayKey = QString::fromUtf8(t.bytes);
				}
			}
		}
	}
	return any;
}

bool parseTikTokLike(const QByteArray &data, TikTokLike &out)
{
	out = TikTokLike();
	bool any = false;
	PbReader reader(data);
	PbField f;
	while (reader.next(f)) {
		if (f.number == 2)
			out.count = static_cast<int>(f.varint);
		else if (f.number == 3)
			out.total = static_cast<qint64>(f.varint);
		else if (f.number == 5) {
			out.user = parseTikTokUser(f.bytes);
			any = true;
		}
	}
	return any;
}
