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

#include <QJsonObject>
#include <QWidget>

#include <array>

class QLabel;
class QTextBrowser;

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

private:
	static constexpr size_t kPlatforms = 4;

	void loadSettings();
	void saveSettings();
	void applySettings();
	void openSettings();
	void appendMessage(const ChatMessage &msg);
	void showPlaceholder();
	void updateStatus(ChatPlatform platform, ConnectorState state, const QString &detail);

	QNetworkAccessManager m_net;
	std::array<ChatConnector *, kPlatforms> m_connectors{};
	std::array<QLabel *, kPlatforms> m_status{};
	std::array<QString, kPlatforms> m_targets;
	QTextBrowser *m_view = nullptr;
	bool m_hasMessages = false;
};
