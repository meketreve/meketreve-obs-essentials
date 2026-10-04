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

#include "bot-engine.hpp"
#include "overlay-server.hpp"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class UnifiedChatDock;

class TexuguitoDock : public QWidget {
	Q_OBJECT

public:
	TexuguitoDock(UnifiedChatDock *chat, QWidget *parent = nullptr);
	~TexuguitoDock() override;

	QString overlayUrl() const;
	/* Puts the chat parade in the current scene (from the Overlays panel). */
	void addBrowserSource();
	/* The web panel's Parade tab: the look and the link; applyPanel takes
	 * what the panel saved and answers the new state. */
	QJsonObject panelState() const;
	QJsonObject applyPanel(const QJsonObject &panel);
	/* The web panel's Chat bot tab: sounds with their price, the wait per
	 * price, the volume and the chat-made commands. applyBotPanel runs one
	 * "action" and answers the new state (and "error" when it failed). An
	 * "import" downloads in the background: "import" in the state says how
	 * it is going. */
	QJsonObject botPanelState() const;
	QJsonObject applyBotPanel(const QJsonObject &panel);
	QString statusText() const;
	/* Copies an old texuguito-seu-bot-amigo folder in; returns a summary. */
	QString importFrom(const QString &dir);
	BotEngine *engine() const { return m_engine; }

private:
	void loadSettings();
	void saveSettings();
	void applyEnabled();
	void onChat(const ChatMessage &msg);
	void onReply(ChatPlatform platform, const QString &text);
	void pollChatters();
	void updateStreamerChannels();
	void refreshStatus();
	QJsonObject lookJson() const;
	void importOldBot();
	void addAudio();
	void openSettings();

	UnifiedChatDock *m_chat;
	BotEngine *m_engine = nullptr;
	OverlayServer *m_server = nullptr;
	QNetworkAccessManager m_net;
	/* The web panel's last import: {state: downloading|done|error, ...}. */
	QJsonObject m_import;
	QTimer m_chattersTimer;
	QLabel *m_status = nullptr;
	QLabel *m_replies = nullptr;
	QPushButton *m_toggle = nullptr;
	QString m_dataDir;
	QString m_audioDir;
	quint16 m_port = 8901;
	double m_volume = 1.0;
	/* The parade's look, set in the web panel. */
	double m_scale = 1.0;
	double m_speed = 1.0;
	bool m_names = true;
	int m_nameSize = 10;
	QHash<int, int> m_cooldowns; /* price -> seconds */
	bool m_enabled = true;
	bool m_chattersDenied = false;
	/* What the bot said lately: the same text coming back through the
	 * chat (it is sent from the streamer's own account) is not a command. */
	QHash<QString, qint64> m_recentReplies;
};
