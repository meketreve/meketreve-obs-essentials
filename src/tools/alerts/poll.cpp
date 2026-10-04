/*
Meketreve OBS Essentials - Alerts
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

#include "poll.hpp"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Poll {

namespace {

double number(const QJsonObject &o, const char *key, double fallback, double low, double high)
{
	const QJsonValue v = o.value(QLatin1String(key));
	const double n = v.isDouble() ? v.toDouble() : fallback;
	return std::isfinite(n) ? std::clamp(n, low, high) : fallback;
}

bool flag(const QJsonObject &o, const char *key, bool fallback)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() ? v.toBool() : fallback;
}

QString color(const QJsonObject &o, const char *key, const QString &fallback)
{
	static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	const QString v = o.value(QLatin1String(key)).toString();
	return re.match(v).hasMatch() ? v : fallback;
}

} // namespace

QJsonObject normalizeConfig(const QJsonObject &stored)
{
	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	return QJsonObject{
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("fontSize"), number(stored, "fontSize", 24, 12, 72)},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("accent"), color(stored, "accent", QStringLiteral("#8B5CF6"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), number(stored, "bubbleOpacity", 45, 0, 100)},
		{QStringLiteral("shadow"), flag(stored, "shadow", true)},
		/* How long the result stays on screen; 0 = until the next poll. */
		{QStringLiteral("resultSeconds"), number(stored, "resultSeconds", 20, 0, 600)},
		{QStringLiteral("announce"), flag(stored, "announce", false)},
	};
}

int voteFromChat(const QString &text)
{
	static const QRegularExpression re(QStringLiteral("^\\s*!vot[oe]\\s+(\\d)\\b"),
					   QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch m = re.match(text);
	return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QString Session::start(const QString &question, const QStringList &options, int seconds, qint64 nowMs)
{
	const QString q = question.trimmed().left(120);
	QStringList clean;
	for (const QString &o : options) {
		const QString text = o.trimmed().left(60);
		if (!text.isEmpty())
			clean.append(text);
	}
	if (q.isEmpty())
		return QStringLiteral("question");
	if (clean.size() < kMinOptions || clean.size() > kMaxOptions)
		return QStringLiteral("options");
	if (seconds < 0 || seconds > kMaxSeconds)
		return QStringLiteral("duration");
	m_question = q;
	m_options = clean;
	m_votes.clear();
	m_open = true;
	m_endsAt = seconds > 0 ? nowMs + qint64(seconds) * 1000 : 0;
	m_closedAt = 0;
	return QString();
}

bool Session::stop(qint64 nowMs)
{
	if (!m_open)
		return false;
	m_open = false;
	m_closedAt = nowMs;
	return true;
}

bool Session::expire(qint64 nowMs)
{
	if (!m_open || m_endsAt == 0 || nowMs < m_endsAt)
		return false;
	m_open = false;
	m_closedAt = m_endsAt;
	return true;
}

bool Session::vote(const QString &voter, int option, qint64 nowMs)
{
	expire(nowMs);
	if (!m_open || voter.isEmpty() || option < 1 || option > m_options.size())
		return false;
	const auto it = m_votes.constFind(voter);
	if (it != m_votes.constEnd() && *it == option - 1)
		return false;
	m_votes.insert(voter, option - 1);
	return true;
}

QList<int> Session::counts() const
{
	QList<int> out(m_options.size(), 0);
	for (const int choice : m_votes)
		if (choice >= 0 && choice < out.size())
			out[choice]++;
	return out;
}

int Session::winner() const
{
	const QList<int> c = counts();
	int best = 0;
	for (int i = 0; i < c.size(); i++)
		if (c.at(i) > 0 && (best == 0 || c.at(i) > c.at(best - 1)))
			best = i + 1;
	return best;
}

QJsonObject Session::toJson(qint64 nowMs) const
{
	if (!exists())
		return QJsonObject();
	const QList<int> c = counts();
	QJsonArray options;
	for (int i = 0; i < m_options.size(); i++)
		options.append(
			QJsonObject{{QStringLiteral("text"), m_options.at(i)}, {QStringLiteral("votes"), c.at(i)}});
	const qint64 remaining = m_open && m_endsAt > 0 ? std::max<qint64>(0, m_endsAt - nowMs) : 0;
	return QJsonObject{{QStringLiteral("question"), m_question},
			   {QStringLiteral("options"), options},
			   {QStringLiteral("total"), static_cast<int>(m_votes.size())},
			   {QStringLiteral("open"), m_open},
			   {QStringLiteral("timed"), m_endsAt > 0},
			   {QStringLiteral("remainingMs"), static_cast<double>(remaining)},
			   {QStringLiteral("closedAt"), static_cast<double>(m_closedAt)},
			   {QStringLiteral("winner"), winner()}};
}

QJsonObject Session::save() const
{
	QJsonObject votes;
	for (auto it = m_votes.constBegin(); it != m_votes.constEnd(); ++it)
		votes.insert(it.key(), it.value());
	return QJsonObject{{QStringLiteral("question"), m_question},
			   {QStringLiteral("options"), QJsonArray::fromStringList(m_options)},
			   {QStringLiteral("votes"), votes},
			   {QStringLiteral("open"), m_open},
			   {QStringLiteral("endsAt"), static_cast<double>(m_endsAt)},
			   {QStringLiteral("closedAt"), static_cast<double>(m_closedAt)}};
}

void Session::load(const QJsonObject &stored)
{
	m_question = stored.value(QStringLiteral("question")).toString().left(120);
	m_options.clear();
	for (const QJsonValue v : stored.value(QStringLiteral("options")).toArray())
		if (m_options.size() < kMaxOptions)
			m_options.append(v.toString().left(60));
	if (m_options.size() < kMinOptions) {
		*this = Session();
		return;
	}
	m_votes.clear();
	const QJsonObject votes = stored.value(QStringLiteral("votes")).toObject();
	for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
		const int choice = it.value().toInt(-1);
		if (choice >= 0 && choice < m_options.size())
			m_votes.insert(it.key(), choice);
	}
	m_open = stored.value(QStringLiteral("open")).toBool();
	m_endsAt = static_cast<qint64>(stored.value(QStringLiteral("endsAt")).toDouble());
	m_closedAt = static_cast<qint64>(stored.value(QStringLiteral("closedAt")).toDouble());
}

} // namespace Poll
