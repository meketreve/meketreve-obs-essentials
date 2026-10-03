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
#include "sound-fetch.hpp"
#include "../unified-chat/chat-connector.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <map>
#include <optional>

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
	/* lang: "pt" or "en", the voice to speak with. */
	using TtsFunction = std::function<void(const QString &text, const QString &lang,
					       std::function<void(QByteArray mp3, QString error)>)>;
	/* Chat replies by locale key ("Texuguito.Bot.*"); OBS answers them in
	 * its UI language. */
	using TextFunction = std::function<QString(const char *key)>;
	/* Both languages for chat replies: the answer follows the language of
	 * the command that was typed (!help answers in English, !ajuda in
	 * Portuguese), not the language OBS is in. Neutral aliases (ping,
	 * status, pts, p, tts, cmd, audio) fall back to TextFunction. */

	static constexpr int kTtsCost = 200;
	static constexpr int kMaxClipCost = 100000;
	static constexpr int kPointsTickSeconds = 60;
	/* Without a viewer list (every platform but a logged-in Twitch), a
	 * viewer counts as present for this long after their last message. */
	static constexpr int kPresenceMinutes = 10;
	static constexpr qint64 kSameCommandMs = 15000;
	/* The site's command list, one page per language. */
	static constexpr const char *kCommandsUrlPt =
		"https://meketreve.github.io/meketreve-obs-essentials/comandos.html";
	static constexpr const char *kCommandsUrlEn =
		"https://meketreve.github.io/meketreve-obs-essentials/commands.html";

	BotEngine(const QString &dataDir, const QString &audioDir, QObject *parent = nullptr);

	static QString keyFor(ChatPlatform platform, const QString &user);

	void setTts(TtsFunction tts) { m_tts = std::move(tts); }
	/* Downloads a sound from a link for !addaudio (SoundFetch::fetch). */
	using SoundFetchFunction =
		std::function<void(const QString &link, std::function<void(const SoundFetch::Result &)> done)>;
	void setSoundFetch(SoundFetchFunction fetch) { m_soundFetch = std::move(fetch); }
	/* Saves a sound as <audio dir>/<cost>/<name>.<ext> for !tocar. Empty on
	 * success, else the reason (in the command's language; empty invoked
	 * means the OBS language, for the dock button). */
	QString addClip(const QByteArray &data, const QString &ext, const QString &name, int cost,
			const QString &invoked = QString());
	/* What a fetch error means, in the bot's language. */
	QString soundFetchError(const SoundFetch::Result &result, const QString &invoked = QString()) const;
	void setText(TextFunction text) { m_text = std::move(text); }
	void setCommandTexts(const QHash<QString, QString> &en, const QHash<QString, QString> &pt)
	{
		m_cmdEn = en;
		m_cmdPt = pt;
	}
	/* True for an English command, false for Portuguese, empty for a
	 * neutral one (ping, status, pts, p, tts, cmd, audio). */
	static std::optional<bool> commandEnglish(const QString &invoked);
	void setVolume(double volume) { m_volume = volume; }
	void setOverlayListeners(int count) { m_listeners = count; }
	/* Seconds between two sounds of the same price (per price folder); a
	 * price without an entry uses defaultCooldownSeconds(). */
	void setClipCooldowns(const QHash<int, int> &seconds) { m_cooldowns = seconds; }
	int clipCooldownSeconds(int cost) const { return m_cooldowns.value(cost, defaultCooldownSeconds(cost)); }
	static int defaultCooldownSeconds(int cost);
	QList<int> clipCosts() const;
	void setAudioDir(const QString &dir);
	QString audioDir() const { return m_audioDir; }
	int reloadClips();
	const std::map<QString, AudioClip> &clips() const { return m_clips; }

	/* Re-reads the JSON files and the audio folder (after an import). */
	void reloadData();

	void handleMessage(const BotMessage &msg);
	/* Bits, Super Chats and Kick gifts make the viewer cheer on screen. */
	void handleCheer(ChatPlatform platform, const QString &user);
	/* Twitch viewer list (Helix chatters): who is watching even if quiet. */
	void setTwitchChatters(const QSet<QString> &logins);
	/* The streamer's own channels: their avatar never leaves the parade. */
	void setStreamerChannels(const QList<QPair<ChatPlatform, QString>> &channels);
	/* Runs the per-minute points tick now (the timer calls it too). */
	void pointsTick();
	void refreshPresence();

	QJsonObject snapshot();
	QByteArray ttsClip(const QString &id) const { return m_ttsClips.value(id); }
	PointsStore &points() { return m_points; }
	ViewerStore &viewers() { return m_viewers; }
	CustomCommandStore &customCommands() { return m_custom; }
	void setRaffleChooser(Raffle::Chooser chooser) { m_raffle = Raffle(std::move(chooser)); }
	/* For tests: move the clip cooldown clocks back. */
	void resetClipCooldown() { m_lastClip.clear(); }

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
		bool streamer = false; /* one of the streamer's channels */
		qint64 lastSeen = 0;
		qint64 presentSince = 0; /* who got the avatar first among same-name viewers */
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
	QString t(const char *key) const { return m_text ? m_text(key) : QString::fromLatin1(key); }
	QString tCmd(const QString &invoked, const char *key) const;
	bool english() const { return t("Texuguito.Bot.Language") == QLatin1String("en"); }
	/* One avatar per name: the same name on two platforms (ana and
	 * kick:ana) is drawn once, by whoever showed up first; the other takes
	 * over when that one leaves. Call after any change to key's status. */
	void syncAvatar(const QString &key);
	void emitAvatar(const QString &type, const QString &key);
	static QString baseName(const QString &key);
	QStringList presentTwins(const QString &base) const;
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
	QSet<QString> m_drawn; /* keys the overlay currently shows */
	QList<Command> m_commands;
	std::map<QString, AudioClip> m_clips;
	QString m_audioDir;
	QHash<int, QElapsedTimer> m_lastClip; /* by price */
	/* A command the streamer sent to every chat at once comes back from
	 * each platform: only the first one runs. */
	struct StreamerCommand {
		QString text;
		ChatPlatform platform = ChatPlatform::Twitch;
		qint64 at = -1;
	};
	StreamerCommand m_lastStreamerCommand;
	QHash<int, int> m_cooldowns;
	QElapsedTimer m_clock;
	double m_volume = 1.0;
	int m_listeners = 0;
	TtsFunction m_tts;
	SoundFetchFunction m_soundFetch;
	TextFunction m_text;
	QHash<QString, QString> m_cmdEn;
	QHash<QString, QString> m_cmdPt;
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
