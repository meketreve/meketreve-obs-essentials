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
#include "chat-html.hpp"

#include <QList>

namespace {

/* Pieces that only glue emoji together or change one: joiner, variation
 * selector, skin tones, keycap, tag characters (subdivision flags). */
bool isEmojiPart(char32_t cp)
{
	return cp == 0x200D || cp == 0xFE0F || (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0x20E3 ||
	       (cp >= 0xE0020 && cp <= 0xE007F);
}

QList<char32_t> codePoints(const QString &text)
{
	QList<char32_t> out;
	out.reserve(text.size());
	for (qsizetype i = 0; i < text.size(); i++) {
		const QChar c = text[i];
		if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
			out.append(QChar::surrogateToUcs4(c, text[i + 1]));
			i++;
		} else {
			out.append(c.unicode());
		}
	}
	return out;
}

QString fromCodePoints(const QList<char32_t> &cps, qsizetype from, qsizetype to)
{
	QString s;
	for (qsizetype i = from; i < to; i++)
		s += QString::fromUcs4(&cps[i], 1);
	return s;
}

} // namespace

bool isEmojiCodePoint(char32_t cp)
{
	return (cp >= 0x1F000 && cp <= 0x1FAFF) || /* pictographs, emoticons, flags, ... */
	       (cp >= 0x2600 && cp <= 0x27BF) ||   /* misc symbols, dingbats */
	       (cp >= 0x2300 && cp <= 0x23FF) ||   /* watch, hourglass, play buttons */
	       (cp >= 0x2B05 && cp <= 0x2B55) ||   /* arrows, star, circle */
	       cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299;
}

namespace {

/* Text with emoji marked. */
QString emojiHtml(const QString &text)
{
	const QList<char32_t> cps = codePoints(text);
	QString html;
	qsizetype plainStart = 0;
	qsizetype i = 0;
	while (i < cps.size()) {
		/* Anything followed by U+FE0F asks to be drawn as emoji (©️, 1️⃣). */
		const bool starts = isEmojiCodePoint(cps[i]) || (i + 1 < cps.size() && cps[i + 1] == 0xFE0F);
		if (!starts) {
			i++;
			continue;
		}
		qsizetype end = i + 1;
		while (end < cps.size()) {
			if (isEmojiPart(cps[end])) {
				end++;
			} else if (cps[end - 1] == 0x200D && isEmojiCodePoint(cps[end])) {
				end++;
			} else if (cps[i] >= 0x1F1E6 && cps[i] <= 0x1F1FF && end == i + 1 && cps[end] >= 0x1F1E6 &&
				   cps[end] <= 0x1F1FF) {
				end++; /* the second regional indicator of a flag */
			} else {
				break;
			}
		}
		html += fromCodePoints(cps, plainStart, i).toHtmlEscaped();
		html += QStringLiteral("<span style=\"font-family:'Noto Color Emoji','Segoe UI Emoji','Apple Color "
				       "Emoji'\">") +
			fromCodePoints(cps, i, end) + QStringLiteral("</span>");
		i = end;
		plainStart = end;
	}
	return html + fromCodePoints(cps, plainStart, cps.size()).toHtmlEscaped();
}

} // namespace

QString chatHtml(const QString &text)
{
	return emojiHtml(text);
}
