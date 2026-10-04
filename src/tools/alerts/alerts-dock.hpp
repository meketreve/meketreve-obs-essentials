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
#include "event-history.hpp"
#include "goals.hpp"
#include "../texuguito/overlay-server.hpp"

#include <obs-frontend-api.h>

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QWidget>

#include <functional>

/* An overlay with its own server (Now Playing) still gets a tab in the web
 * panel: get answers the tab's state, set applies what the panel saved and
 * answers the new state. Served at /api/tab/<name>. */
struct PanelTab {
	std::function<QJsonObject()> get;
	std::function<QJsonObject(const QJsonObject &)> set;
};
void overlaysAddPanelTab(const QString &name, PanelTab tab);

/* An overlay with its own server also gets a section in the Overlays panel:
 * its title, a line of help, "Add to scene" and "Copy link". Tools that load
 * before the panel exists are queued. */
struct OverlaySection {
	QString title;
	QString help;
	std::function<QString()> url;
	std::function<void()> addToScene;
};
void overlaysAddSection(OverlaySection section);

class QLabel;
class QPushButton;
class QVBoxLayout;
class UnifiedChatDock;

class AlertsDock : public QWidget {
	Q_OBJECT

public:
	AlertsDock(UnifiedChatDock *chat, QWidget *parent = nullptr);
	~AlertsDock() override;

	QString overlayUrl() const;
	QString chatOverlayUrl() const;
	QString eventsOverlayUrl() const;
	QString goalsOverlayUrl() const;
	/* The web panel, open on one tab: alertas, chat, eventos, tocando. */
	QString panelUrl(const QString &tab) const;
	QJsonObject shareableConfig() const;
	void importConfig(const QJsonObject &config);
	QJsonObject chatConfig() const { return m_chatConfig; }
	void importChatConfig(const QJsonObject &config);
	QJsonObject eventsConfig() const { return m_eventsConfig; }
	void addSection(const OverlaySection &section);
	void importEventsConfig(const QJsonObject &config);

private:
	void loadSettings();
	void saveSettings();
	void loadConfig();
	void saveConfig();
	void saveChatConfig();
	void saveHistory();
	/* The latest events and the labels, to every events overlay. */
	void broadcastHistory();
	QJsonObject historyMessage(const QString &type) const;
	/* One made-up event of each kind and the labels they make, for the
	 * events editor's preview. */
	QJsonObject eventsSamples() const;
	static void frontendEvent(enum obs_frontend_event event, void *data);
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
	/* Goals (and the other overlays the events fill), in alerts-widgets.cpp:
	 * load at start, count a real event, a new live, the snapshot a page gets
	 * when it connects, and their pages and /api routes (after the token
	 * check). */
	void loadWidgets();
	void widgetsEvent(const Alerts::Event &event);
	void widgetsLiveStarted();
	void widgetsSnapshot(QJsonObject &snapshot) const;
	bool widgetsPage(const QString &path, OverlayServer::Reply &reply) const;
	bool widgetsApi(const OverlayServer::Request &request, OverlayServer::Reply &reply);
	void saveGoals();
	void broadcastGoals();
	QJsonObject goalsMessage() const;
	void apiMediaList(OverlayServer::Reply &reply) const;
	void apiMediaUpload(const QString &name, const QByteArray &data, OverlayServer::Reply &reply);

	UnifiedChatDock *m_chat;
	OverlayServer *m_server = nullptr;
	QNetworkAccessManager m_net;
	QLabel *m_status = nullptr;
	QVBoxLayout *m_extraSections = nullptr;
	QPushButton *m_toggle = nullptr;
	QString m_dir;
	QString m_mediaDir;
	QJsonObject m_config;
	QJsonObject m_chatConfig;
	QJsonObject m_eventsConfig;
	EventsOverlay::History m_history;
	QJsonObject m_goals;
	QString m_token;
	quint16 m_port = 8902;
	bool m_enabled = true;
	Alerts::GiftDedup m_dedup;
	Alerts::GiftDedup m_chatDedup;
	QHash<QString, QByteArray> m_tts;
	QStringList m_ttsOrder;
	int m_ttsSerial = 0;
};
