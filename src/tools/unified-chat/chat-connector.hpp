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

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

enum class ChatPlatform { Twitch, YouTube, Kick, TikTok };

struct ChatMessage {
	ChatPlatform platform;
	QString author;
	QString authorColor;
	QString text;
	QString highlight;
};

enum class ConnectorState { Idle, Connecting, Connected, Offline, Error };

extern const char *const kBrowserUserAgent;

class ChatConnector : public QObject {
	Q_OBJECT

public:
	ChatConnector(ChatPlatform platform, QNetworkAccessManager *net, QObject *parent);

	ChatPlatform platform() const { return m_platform; }

	void start(const QString &target);
	void stop();

signals:
	void messageReceived(const ChatMessage &msg);
	void stateChanged(ConnectorState state, const QString &detail);

protected:
	virtual void connectNow() = 0;
	virtual void disconnectNow() = 0;

	void setState(ConnectorState state, const QString &detail = QString());
	void emitMessage(const QString &author, const QString &color, const QString &text,
			 const QString &highlight = QString());
	void scheduleRetry(int seconds);
	void scheduleReconnect();
	void markHealthy();

	QString target() const { return m_target; }
	bool running() const { return m_running; }
	QNetworkAccessManager *net() const { return m_net; }

private:
	ChatPlatform m_platform;
	QNetworkAccessManager *m_net;
	QString m_target;
	QTimer m_retryTimer;
	int m_backoff = 0;
	bool m_running = false;
};
