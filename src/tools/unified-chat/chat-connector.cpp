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

#include "chat-connector.hpp"

#include <algorithm>

const char *const kBrowserUserAgent =
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

ChatConnector::ChatConnector(ChatPlatform platform, QNetworkAccessManager *net, QObject *parent)
	: QObject(parent),
	  m_platform(platform),
	  m_net(net)
{
	m_retryTimer.setSingleShot(true);
	connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
		if (m_running)
			connectNow();
	});
}

void ChatConnector::start(const QString &target)
{
	stop();
	m_target = target.trimmed();
	if (m_target.isEmpty())
		return;
	m_running = true;
	m_backoff = 0;
	setState(ConnectorState::Connecting);
	connectNow();
}

void ChatConnector::stop()
{
	m_retryTimer.stop();
	if (m_running) {
		m_running = false;
		disconnectNow();
	}
	setState(ConnectorState::Idle);
}

void ChatConnector::setState(ConnectorState state, const QString &detail)
{
	emit stateChanged(state, detail);
}

void ChatConnector::emitMessage(const QString &author, const QString &color, const QString &text,
				const QString &highlight)
{
	emit messageReceived(ChatMessage{m_platform, author, color, text, highlight});
}

void ChatConnector::scheduleRetry(int seconds)
{
	if (!m_running)
		return;
	disconnectNow();
	m_retryTimer.start(seconds * 1000);
}

void ChatConnector::scheduleReconnect()
{
	/* 2, 4, 8, 16, 32, 60, 60... seconds between failed attempts. */
	m_backoff = std::min(m_backoff == 0 ? 2 : m_backoff * 2, 60);
	scheduleRetry(m_backoff);
}

void ChatConnector::markHealthy()
{
	m_backoff = 0;
	setState(ConnectorState::Connected);
}
