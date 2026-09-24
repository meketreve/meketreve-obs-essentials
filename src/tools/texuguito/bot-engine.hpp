/*
Meketreve OBS Essentials - Texuguito bot
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

#include "bot-data.hpp"
#include "../unified-chat/chat-connector.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <map>

/* A chat line as the bot sees it, from any platform. */
struct BotMessage {
	ChatPlatform platform = ChatPlatform::Twitch;
	QString user; /* display name */
	QString text;
	bool isMod = false;
	bool isSub = false;
	bool isBroadcaster = false;
	/* Twitch prepends "@user " to Reply messages; the bot skips it. */
	bool isReply = false;
};

struct AudioClip {
	QString name;
	int cost = 0;
	QString url; /* "/audios/<cost>/<file>" */
};

/* Texuguito: the chat parade overlay, channel points, soundboard, TTS,
 * raffles and chat-made commands, ported from texuguito-seu-bot-amigo.
 * Reads every platform's chat; replies go back to the platform the command
 * came from. No OBS dependency, so it runs in unit tests. */
class BotEngine : public QObject {
	Q_OBJECT

public:
	using TtsFunction =
		std::function<void(const QString &text, std::function<void(QByteArray mp3, QString error)>)>;

	static constexpr int kClipCooldownSeconds = 60;
	static constexpr int kTtsCost = 200;
	static constexpr int kPointsTickSeconds = 60;
	/* Without a viewer list (every platform but a logged-in Twitch), a
	 * viewer counts as present for this long after their last message. */
	static constexpr int kPresenceMinutes = 10;

	BotEngine(const QString &dataDir, const QString &audioDir, QObject *parent = nullptr);

	static QString keyFor(ChatPlatform platform, const QString &user);

	void setTts(TtsFunction tts) { m_tts = std::move(tts); }
	void setVolume(double volume) { m_volume = volume; }
	void setOverlayListeners(int count) { m_listeners = count; }
	void setAudioDir(const QString &dir);
	QString audioDir() const { return m_audioDir; }
	int reloadClips();
	const std::map<QString, AudioClip> &clips() const { return m_clips; }

	/* Re-reads the JSON files and the audio folder (after an import). */
	void reloadData();

	void handleMessage(const BotMessage &msg);
	/* Bits, Super Chats and TikTok gifts make the viewer cheer on screen. */
	void handleCheer(ChatPlatform platform, const QString &user);
	/* Twitch viewer list (Helix chatters): who is watching even if quiet. */
	void setTwitchChatters(const QSet<QString> &logins);
	/* Runs the per-minute points tick now (the timer calls it too). */
	void pointsTick();
	void refreshPresence();

	QJsonObject snapshot();
	QByteArray ttsClip(const QString &id) const { return m_ttsClips.value(id); }
	PointsStore &points() { return m_points; }
	ViewerStore &viewers() { return m_viewers; }
	CustomCommandStore &customCommands() { return m_custom; }
	void setRaffleChooser(Raffle::Chooser chooser) { m_raffle = Raffle(std::move(chooser)); }
	/* For tests: move the clip cooldown clock back. */
	void resetClipCooldown() { m_lastClip.invalidate(); }

signals:
	void reply(ChatPlatform platform, const QString &text);
	void overlayMessage(const QJsonObject &message);

private:
	struct Status {
		ChatPlatform platform = ChatPlatform::Twitch;
		bool isMod = false;
		bool isSub = false;
		bool isBroadcaster = false;
		bool present = false;
		bool inChatters = false;
		qint64 lastSeen = 0;
		qint64 dancingUntil = 0;
		qint64 cheerUntil = 0;
	};
	using Handler = std::function<void(const BotMessage &, const QString &key, const QStringList &args)>;
	struct Command {
		QString name;
		QStringList aliases;
		Handler handler;
	};

	void registerCommands();
	const Command *findCommand(const QString &name) const;
	QSet<QString> reservedNames() const;
	void say(ChatPlatform platform, const QString &text);
	void viewerEvent(const QString &type, const QString &key);
	QJsonObject viewerPayload(const QString &key);
	QString displayName(const QString &key);
	bool privileged(const BotMessage &msg) const { return msg.isMod || msg.isBroadcaster; }
	QString handleCustomCommand(const BotMessage &msg, const QString &name, const QStringList &args);
	QString handleComando(const BotMessage &msg, const QStringList &args);
	void playTts(const BotMessage &msg, const QString &key, const QStringList &args);

	ViewerStore m_viewers;
	PointsStore m_points;
	CustomCommandStore m_custom;
	Raffle m_raffle;
	QHash<QString, Status> m_status;
	QList<Command> m_commands;
	std::map<QString, AudioClip> m_clips;
	QString m_audioDir;
	QElapsedTimer m_lastClip;
	QElapsedTimer m_clock;
	double m_volume = 1.0;
	int m_listeners = 0;
	TtsFunction m_tts;
	QHash<QString, QByteArray> m_ttsClips;
	QStringList m_ttsOrder;
	QSet<QString> m_lastTickPresent;
	QTimer m_pointsTimer;
	QTimer m_presenceTimer;
};

namespace BotText {
QString truncate(const QString &text, int limit = 450);
/* "!nome a b" -> {"nome", ["a", "b"]}; empty name when it is not a command. */
std::pair<QString, QStringList> parseInvocation(const QString &content, bool isReply);
} // namespace BotText
