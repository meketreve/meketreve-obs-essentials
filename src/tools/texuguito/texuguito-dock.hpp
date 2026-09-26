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
	void refreshStatus();
	void addBrowserSource();
	void importOldBot();
	void openSettings();

	UnifiedChatDock *m_chat;
	BotEngine *m_engine = nullptr;
	OverlayServer *m_server = nullptr;
	QNetworkAccessManager m_net;
	QTimer m_chattersTimer;
	QLabel *m_status = nullptr;
	QLabel *m_replies = nullptr;
	QPushButton *m_toggle = nullptr;
	QString m_dataDir;
	QString m_audioDir;
	quint16 m_port = 8901;
	double m_volume = 1.0;
	QHash<int, int> m_cooldowns; /* price -> seconds */
	bool m_enabled = true;
	bool m_chattersDenied = false;
	/* What the bot said lately: the same text coming back through the
	 * chat (it is sent from the streamer's own account) is not a command. */
	QHash<QString, qint64> m_recentReplies;
};
