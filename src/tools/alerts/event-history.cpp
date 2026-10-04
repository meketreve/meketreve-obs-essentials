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

#include "event-history.hpp"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace EventsOverlay {

namespace {

struct LabelKind {
	const char *kind;
	const char *textKey; /* default text */
	QStringList types;   /* empty for the "top" ones */
};

const QList<LabelKind> &kinds()
{
	static const QList<LabelKind> list{
		{"ultimo-follow", "EventsOverlay.Default.LastFollow", {QStringLiteral("follow")}},
		{"ultimo-sub", "EventsOverlay.Default.LastSub", {QStringLiteral("sub"), QStringLiteral("resub")}},
		{"ultimo-presente",
		 "EventsOverlay.Default.LastGift",
		 {QStringLiteral("giftsub"), QStringLiteral("gift")}},
		{"ultima-doacao", "EventsOverlay.Default.LastDonation", {QStringLiteral("donation")}},
		{"ultimo-raid", "EventsOverlay.Default.LastRaid", {QStringLiteral("raid")}},
		{"ultimo-bits", "EventsOverlay.Default.LastBits", {QStringLiteral("bits")}},
		{"ultimo-membro", "EventsOverlay.Default.LastMember", {QStringLiteral("membership")}},
		{"top-doador", "EventsOverlay.Default.TopDonor", {}},
		{"top-bits", "EventsOverlay.Default.TopBits", {}},
	};
	return list;
}

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

/* "R$ 10,00" -> "R$ ", "$5.00" -> "$", "10 €" -> "". */
QString moneyPrefix(const QString &amount)
{
	const qsizetype digit = amount.indexOf(QRegularExpression(QStringLiteral("[0-9]")));
	return digit > 0 ? amount.left(digit) : QString();
}

} // namespace

QStringList labelKinds()
{
	QStringList out;
	for (const LabelKind &k : kinds())
		out.append(QLatin1String(k.kind));
	return out;
}

QJsonObject defaults(const Alerts::TextLookup &text)
{
	return normalize(QJsonObject(), text);
}

QJsonObject normalize(const QJsonObject &stored, const Alerts::TextLookup &text)
{
	const QJsonObject inList = stored.value(QStringLiteral("list")).toObject();
	const QJsonObject inTypes = inList.value(QStringLiteral("types")).toObject();
	QJsonObject types;
	for (const QString &type : Alerts::types())
		types.insert(type, inTypes.value(type).toBool(true));

	const QJsonObject inLabels = stored.value(QStringLiteral("labels")).toObject();
	QJsonObject labels;
	for (const LabelKind &k : kinds()) {
		const QString kind = QLatin1String(k.kind);
		const QJsonValue v = inLabels.value(kind);
		labels.insert(kind, v.isString() ? v.toString().left(120) : text(k.textKey));
	}

	const QString font = stored.value(QStringLiteral("font")).toString().trimmed().left(60);
	const QJsonValue empty = stored.value(QStringLiteral("emptyText"));
	return QJsonObject{
		{QStringLiteral("list"), QJsonObject{{QStringLiteral("types"), types},
						     {QStringLiteral("max"), number(inList, "max", 5, 1, 20)}}},
		{QStringLiteral("labels"), labels},
		{QStringLiteral("emptyText"), empty.isString() ? empty.toString().left(40) : QStringLiteral("—")},
		{QStringLiteral("resetTopOnLive"), flag(stored, "resetTopOnLive", true)},
		{QStringLiteral("font"), font.isEmpty() ? QStringLiteral("Poppins") : font},
		{QStringLiteral("fontSize"), number(stored, "fontSize", 24, 12, 72)},
		{QStringLiteral("textColor"), color(stored, "textColor", QStringLiteral("#FFFFFF"))},
		{QStringLiteral("accent"), color(stored, "accent", QStringLiteral("#8B5CF6"))},
		{QStringLiteral("bubbleColor"), color(stored, "bubbleColor", QStringLiteral("#000000"))},
		{QStringLiteral("bubbleOpacity"), number(stored, "bubbleOpacity", 45, 0, 100)},
		{QStringLiteral("shadow"), flag(stored, "shadow", true)},
		{QStringLiteral("showPlatform"), flag(stored, "showPlatform", true)},
	};
}

Entry entryFrom(const Alerts::Event &event, const QString &text, const QString &id, qint64 nowMs)
{
	Entry e;
	e.id = id;
	e.platform = event.platform;
	e.type = event.type;
	e.name = event.name;
	e.amount = event.amount;
	e.value = event.value;
	e.message = event.message;
	e.text = text;
	e.at = nowMs;
	return e;
}

QJsonObject toJson(const Entry &entry)
{
	return QJsonObject{{QStringLiteral("id"), entry.id},
			   {QStringLiteral("platform"), entry.platform},
			   {QStringLiteral("type"), entry.type},
			   {QStringLiteral("name"), entry.name},
			   {QStringLiteral("amount"), entry.amount},
			   {QStringLiteral("value"), entry.value},
			   {QStringLiteral("message"), entry.message},
			   {QStringLiteral("text"), entry.text},
			   {QStringLiteral("at"), static_cast<double>(entry.at)}};
}

void History::add(const Entry &entry)
{
	m_entries.append(entry);
	while (m_entries.size() > kKeep)
		m_entries.removeFirst();
	for (const LabelKind &k : kinds()) {
		if (k.types.contains(entry.type))
			m_last.insert(QLatin1String(k.kind), entry);
	}
	/* Same name on two platforms counts as two people, like the points. */
	const QString who = entry.platform + QLatin1Char(':') + entry.name.trimmed().toLower();
	QHash<QString, Total> *totals = entry.type == QLatin1String("donation") ? &m_donors
					: entry.type == QLatin1String("bits")   ? &m_bits
										: nullptr;
	if (!totals || entry.value <= 0)
		return;
	Total &t = (*totals)[who];
	t.name = entry.name;
	t.platform = entry.platform;
	if (t.prefix.isEmpty())
		t.prefix = moneyPrefix(entry.amount);
	t.sum += entry.value;
}

void History::resetTop()
{
	m_donors.clear();
	m_bits.clear();
}

QJsonArray History::recent(int count) const
{
	QJsonArray out;
	for (qsizetype i = m_entries.size() - 1; i >= 0 && out.size() < count; i--)
		out.append(toJson(m_entries[i]));
	return out;
}

QString History::money(const Total &total)
{
	return total.prefix + QLocale().toString(total.sum, 'f', 2);
}

QJsonObject History::labels() const
{
	QJsonObject out;
	for (const LabelKind &k : kinds()) {
		const QString kind = QLatin1String(k.kind);
		if (!k.types.isEmpty()) {
			const auto last = m_last.constFind(kind);
			if (last != m_last.constEnd())
				out.insert(kind, QJsonObject{{QStringLiteral("name"), last->name},
							     {QStringLiteral("amount"), last->amount},
							     {QStringLiteral("platform"), last->platform}});
			continue;
		}
		const bool donors = kind == QLatin1String("top-doador");
		const QHash<QString, Total> &totals = donors ? m_donors : m_bits;
		const Total *best = nullptr;
		for (auto it = totals.constBegin(); it != totals.constEnd(); ++it) {
			if (!best || it->sum > best->sum)
				best = &it.value();
		}
		if (best)
			out.insert(kind, QJsonObject{{QStringLiteral("name"), best->name},
						     {QStringLiteral("amount"),
						      donors ? money(*best) : QString::number(qRound64(best->sum))},
						     {QStringLiteral("platform"), best->platform}});
	}
	return out;
}

QJsonObject History::save() const
{
	QJsonArray entries;
	for (const Entry &e : m_entries)
		entries.append(toJson(e));
	const auto totals = [](const QHash<QString, Total> &map) {
		QJsonArray out;
		for (auto it = map.constBegin(); it != map.constEnd(); ++it)
			out.append(QJsonObject{{QStringLiteral("key"), it.key()},
					       {QStringLiteral("name"), it->name},
					       {QStringLiteral("platform"), it->platform},
					       {QStringLiteral("prefix"), it->prefix},
					       {QStringLiteral("sum"), it->sum}});
		return out;
	};
	QJsonObject last;
	for (auto it = m_last.constBegin(); it != m_last.constEnd(); ++it)
		last.insert(it.key(), toJson(it.value()));
	return QJsonObject{{QStringLiteral("entries"), entries},
			   {QStringLiteral("last"), last},
			   {QStringLiteral("donors"), totals(m_donors)},
			   {QStringLiteral("bits"), totals(m_bits)}};
}

void History::load(const QJsonObject &stored)
{
	const auto parse = [](const QJsonObject &o) {
		Entry e;
		e.id = o.value(QStringLiteral("id")).toString();
		e.platform = o.value(QStringLiteral("platform")).toString();
		e.type = o.value(QStringLiteral("type")).toString();
		e.name = o.value(QStringLiteral("name")).toString();
		e.amount = o.value(QStringLiteral("amount")).toString();
		e.value = o.value(QStringLiteral("value")).toDouble();
		e.message = o.value(QStringLiteral("message")).toString();
		e.text = o.value(QStringLiteral("text")).toString();
		e.at = static_cast<qint64>(o.value(QStringLiteral("at")).toDouble());
		return e;
	};
	m_entries.clear();
	for (const QJsonValue v : stored.value(QStringLiteral("entries")).toArray()) {
		const Entry e = parse(v.toObject());
		if (Alerts::isType(e.type))
			m_entries.append(e);
	}
	m_last.clear();
	const QJsonObject last = stored.value(QStringLiteral("last")).toObject();
	for (const QString &kind : labelKinds()) {
		if (last.contains(kind))
			m_last.insert(kind, parse(last.value(kind).toObject()));
	}
	while (m_entries.size() > kKeep)
		m_entries.removeFirst();
	const auto totals = [](const QJsonValue &v) {
		QHash<QString, Total> out;
		for (const QJsonValue t : v.toArray()) {
			const QJsonObject o = t.toObject();
			out.insert(o.value(QStringLiteral("key")).toString(),
				   Total{o.value(QStringLiteral("name")).toString(),
					 o.value(QStringLiteral("platform")).toString(),
					 o.value(QStringLiteral("prefix")).toString(),
					 o.value(QStringLiteral("sum")).toDouble()});
		}
		return out;
	};
	m_donors = totals(stored.value(QStringLiteral("donors")));
	m_bits = totals(stored.value(QStringLiteral("bits")));
}

} // namespace EventsOverlay
