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
#pragma once

#include "alert-logic.hpp"
#include "../texuguito/overlay-server.hpp"

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class UnifiedChatDock;

class AlertsDock : public QWidget {
	Q_OBJECT

public:
	AlertsDock(UnifiedChatDock *chat, QWidget *parent = nullptr);
	~AlertsDock() override;

	QString overlayUrl() const;
	QString editorUrl() const;
	QString chatOverlayUrl() const;
	QString chatEditorUrl() const;
	QJsonObject shareableConfig() const;
	void importConfig(const QJsonObject &config);
	QJsonObject chatConfig() const { return m_chatConfig; }
	void importChatConfig(const QJsonObject &config);

private:
	void loadSettings();
	void saveSettings();
	void loadConfig();
	void saveConfig();
	void saveChatConfig();
	void onShown(const ChatMessage &msg);
	void onActivity(const ChatMessage &msg, const QString &description);
	/* One made-up event of each kind, as the chat on screen shows them. */
	QJsonArray chatEventSamples() const;
	void onRemoved(ChatPlatform platform, const QString &messageId, const QString &userId, bool all);
	void applyEnabled();
	void refreshStatus();
	void onChat(const ChatMessage &msg);
	void fire(const Alerts::Event &event);
	void broadcastConfig();
	QJsonObject overlayConfig() const;
	void addBrowserSource(const QString &name, const QString &url, int width, int height, bool audio);
	void openSettings();
	bool route(const OverlayServer::Request &request, OverlayServer::Reply &reply);
	void apiMediaList(OverlayServer::Reply &reply) const;
	void apiMediaUpload(const QString &name, const QByteArray &data, OverlayServer::Reply &reply);

	UnifiedChatDock *m_chat;
	OverlayServer *m_server = nullptr;
	QNetworkAccessManager m_net;
	QLabel *m_status = nullptr;
	QPushButton *m_toggle = nullptr;
	QString m_dir;
	QString m_mediaDir;
	QJsonObject m_config;
	QJsonObject m_chatConfig;
	QString m_token;
	quint16 m_port = 8902;
	bool m_enabled = true;
	Alerts::GiftDedup m_dedup;
	Alerts::GiftDedup m_chatDedup;
	QHash<QString, QByteArray> m_tts;
	QStringList m_ttsOrder;
	int m_ttsSerial = 0;
};
