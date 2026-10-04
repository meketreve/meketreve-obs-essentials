/*
Meketreve OBS Essentials - Now Playing
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

#include "media-info.hpp"

#ifdef MEKETREVE_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QStringList>
#include <QVariantMap>
#endif

QJsonObject MediaInfo::toJson() const
{
	if (!isValid())
		return {};
	return QJsonObject{{QStringLiteral("player"), player}, {QStringLiteral("playing"), playing},
			   {QStringLiteral("title"), title},   {QStringLiteral("artist"), artist},
			   {QStringLiteral("album"), album},   {QStringLiteral("art"), art},
			   {QStringLiteral("length"), length}, {QStringLiteral("position"), position}};
}

#ifdef MEKETREVE_HAVE_DBUS

namespace {

constexpr int kTimeoutMs = 400;
const QString kMpris = QStringLiteral("org.mpris.MediaPlayer2");

QVariant unwrap(const QVariant &value)
{
	if (value.canConvert<QDBusVariant>())
		return value.value<QDBusVariant>().variant();
	return value;
}

QVariantMap toMap(const QVariant &value)
{
	const QVariant v = unwrap(value);
	if (v.canConvert<QDBusArgument>())
		return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
	return v.toMap();
}

QStringList playerNames(const QDBusConnection &bus)
{
	QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
							   QStringLiteral("/org/freedesktop/DBus"),
							   QStringLiteral("org.freedesktop.DBus"),
							   QStringLiteral("ListNames"));
	const QDBusMessage reply = bus.call(call, QDBus::Block, kTimeoutMs);
	QStringList names;
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
		return names;
	for (const QString &name : reply.arguments().first().toStringList())
		if (name.startsWith(kMpris + QLatin1Char('.')))
			names.append(name);
	return names;
}

MediaInfo readPlayer(const QDBusConnection &bus, const QString &name)
{
	MediaInfo info;
	QDBusMessage call = QDBusMessage::createMethodCall(name, QStringLiteral("/org/mpris/MediaPlayer2"),
							   QStringLiteral("org.freedesktop.DBus.Properties"),
							   QStringLiteral("GetAll"));
	call << kMpris + QStringLiteral(".Player");
	const QDBusMessage reply = bus.call(call, QDBus::Block, kTimeoutMs);
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
		return info;
	const QVariantMap props = toMap(reply.arguments().first());
	const QString status = unwrap(props.value(QStringLiteral("PlaybackStatus"))).toString();
	if (status != QLatin1String("Playing") && status != QLatin1String("Paused"))
		return info;

	const QVariantMap meta = toMap(props.value(QStringLiteral("Metadata")));
	const auto text = [&meta](const char *key) {
		return unwrap(meta.value(QString::fromLatin1(key))).toString();
	};
	const QVariant artist = unwrap(meta.value(QStringLiteral("xesam:artist")));
	info.player = name.mid(kMpris.size() + 1).section(QLatin1Char('.'), 0, 0);
	info.playing = status == QLatin1String("Playing");
	info.title = text("xesam:title");
	info.artist = artist.canConvert<QStringList>() ? artist.toStringList().join(QStringLiteral(", "))
						       : artist.toString();
	info.album = text("xesam:album");
	info.art = text("mpris:artUrl");
	info.length = unwrap(meta.value(QStringLiteral("mpris:length"))).toDouble() / 1e6;
	info.position = unwrap(props.value(QStringLiteral("Position"))).toDouble() / 1e6;
	return info;
}

} // namespace

MediaInfo currentMedia()
{
	const QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.isConnected())
		return {};
	MediaInfo paused;
	for (const QString &name : playerNames(bus)) {
		MediaInfo info = readPlayer(bus, name);
		if (!info.isValid())
			continue;
		if (info.playing)
			return info;
		if (!paused.isValid())
			paused = info;
	}
	return paused;
}

#else

MediaInfo currentMedia()
{
	return {};
}

#endif
