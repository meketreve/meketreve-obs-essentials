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

#pragma once

#include "chat-connector.hpp"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <array>

class ChatAccounts;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QUrl;
class QLineEdit;
class QLabel;
class ChatView;
class EmoteSets;
class ViewersDialog;

/* Subs, gifts, raids, follows... from every platform, one line each. */
class ActivityDock : public QWidget {
	Q_OBJECT

public:
	explicit ActivityDock(QWidget *parent = nullptr);

	void addEvent(const ChatMessage &msg, const QString &description);

private:
	void showPlaceholder();

	ChatView *m_view = nullptr;
	bool m_hasEvents = false;
};

class UnifiedChatDock : public QWidget {
	Q_OBJECT

public:
	explicit UnifiedChatDock(QWidget *parent = nullptr);
	~UnifiedChatDock() override;

	void shutdown();

	/* Channel names for the shareable configuration. */
	QJsonObject exportChannels() const;
	void importChannels(const QJsonObject &channels);
	static QString describeChannels(const QJsonObject &channels);

	/* Entry point for every connector message (public for the harness). */
	void appendMessage(const ChatMessage &msg);

	/* Translated one-line summary of an event ("x gifted 5 subs"). */
	static QString describeEvent(const ChatMessage &msg);

	/* Sends from the logged-in account; false when that platform has no
	 * login or no channel set. */
	bool sendAs(ChatPlatform platform, const QString &text);
	ChatAccounts *accounts() const { return m_accounts; }
	QString target(ChatPlatform platform) const;
	/* Changes one platform's channel as if typed in Settings. */
	void setTarget(ChatPlatform platform, const QString &value);

signals:
	void activity(const ChatMessage &msg, const QString &description);
	/* Every message and event from every platform, before any filtering. */
	void incoming(const ChatMessage &msg);
	/* The channels changed (Settings, import or setTarget). */
	void targetsChanged();

private:
	static constexpr size_t kPlatforms = 4;

	void loadSettings();
	void saveSettings();
	void applySettings();
	void openSettings();
	void openViewers();
	void appendEventLine(const ChatMessage &msg, const QString &description);
	/* Adds BTTV/7TV emotes to what the platform already marked. */
	void addEmotes(ChatMessage &msg);
	void appendSystemLine(ChatPlatform platform, const QString &text);
	bool canModerate(const ChatMessage &msg) const;
	quint64 remember(const ChatMessage &msg);
	void onAuthorClicked(const QUrl &url);
	void updateSendBar();
	void sendInput();
	void addAccountRows(QFormLayout *form, QWidget *dialog);
	void showPlaceholder();
	void updateStatus(ChatPlatform platform, ConnectorState state, const QString &detail);

	QNetworkAccessManager m_net;
	EmoteSets *m_emotes = nullptr;
	std::array<ChatConnector *, kPlatforms> m_connectors{};
	std::array<QLabel *, kPlatforms> m_status{};
	std::array<QString, kPlatforms> m_targets;
	ChatView *m_view = nullptr;
	bool m_hasMessages = false;
	ChatAccounts *m_accounts = nullptr;
	QWidget *m_sendBar = nullptr;
	QComboBox *m_sendTarget = nullptr;
	QLineEdit *m_input = nullptr;
	/* Recent messages by link id, for the moderation menu. */
	QHash<quint64, ChatMessage> m_recent;
	QList<quint64> m_recentOrder;
	quint64 m_lastRecentId = 0;
	QString m_warnedPrivateLive; /* YouTube video id already warned about */
	bool m_eventsInChat = true;
	QPointer<ViewersDialog> m_viewers;
};

/* The dock created at load, or null. */
UnifiedChatDock *unifiedChatDock();
