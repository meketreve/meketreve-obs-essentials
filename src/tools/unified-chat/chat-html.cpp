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
#include <QRegularExpression>
#include <QUrl>

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

/* Plain text (no emotes, no links) with emoji marked. */
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

/* Text between emotes: links, then emoji. */
QString textHtml(const QString &text)
{
	static const QRegularExpression linkRe(QStringLiteral("(?:https?://|www\\.)[^\\s<>\"]+"),
					       QRegularExpression::CaseInsensitiveOption);
	QString html;
	qsizetype last = 0;
	QRegularExpressionMatchIterator it = linkRe.globalMatch(text);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		QString link = m.captured();
		/* Punctuation right after a link belongs to the sentence. */
		while (!link.isEmpty()) {
			const QChar c = link.back();
			const bool unbalanced = (c == QLatin1Char(')') && link.count(QLatin1Char('(')) < link.count(c));
			if (QStringLiteral(".,;:!?'\"]}").contains(c) || unbalanced)
				link.chop(1);
			else
				break;
		}
		const QString target = linkTarget(link);
		if (target.isEmpty())
			continue;
		html += emojiHtml(text.mid(last, m.capturedStart() - last));
		html += QStringLiteral("<a href=\"%1\" style=\"color:#4FC3F7;text-decoration:underline\">%2</a>")
				.arg(target.toHtmlEscaped(), link.toHtmlEscaped());
		last = m.capturedStart() + link.size();
	}
	return html + emojiHtml(text.mid(last));
}

} // namespace

QString linkTarget(const QString &linkText)
{
	QString s = linkText;
	if (s.startsWith(QLatin1String("www."), Qt::CaseInsensitive))
		s.prepend(QStringLiteral("https://"));
	const QUrl url(s, QUrl::StrictMode);
	if (!url.isValid() || url.host().isEmpty() ||
	    (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
		return QString();
	return QString::fromUtf8(url.toEncoded());
}

QString chatHtml(const QString &text, const QList<ChatEmote> &emotes, int emoteHeight)
{
	QString html;
	qsizetype last = 0;
	for (const ChatEmote &e : emotes) {
		if (e.start < last || e.length <= 0 || e.start + e.length > text.size() || e.url.isEmpty())
			continue;
		html += textHtml(text.mid(last, e.start - last));
		html += QStringLiteral("<img src=\"%1\" height=\"%2\" alt=\"%3\" style=\"vertical-align:middle\">")
				.arg(e.url.toHtmlEscaped())
				.arg(emoteHeight)
				.arg(text.mid(e.start, e.length).toHtmlEscaped());
		last = e.start + e.length;
	}
	return html + textHtml(text.mid(last));
}
