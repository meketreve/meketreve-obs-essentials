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

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSslSocket>
#include <QUrl>

/* Minimal RFC 6455 client. OBS ships QtNetwork on every platform but not
 * QtWebSockets, so the handshake and framing are done here by hand. */
class WsClient : public QObject {
	Q_OBJECT

public:
	using Headers = QList<QPair<QByteArray, QByteArray>>;

	explicit WsClient(QObject *parent = nullptr);

	void open(const QUrl &url, const Headers &headers = {});
	void close();
	bool isOpen() const { return m_state == State::Open; }

	void sendText(const QByteArray &utf8);
	void sendBinary(const QByteArray &data);

signals:
	void opened();
	void textReceived(const QByteArray &utf8);
	void binaryReceived(const QByteArray &data);
	void closed(const QString &reason);

private:
	enum class State { Closed, Connecting, Handshake, Open };

	void onEncrypted();
	void onReadyRead();
	void onSocketError();
	void onDisconnected();
	bool readHandshake();
	bool readFrames();
	void sendFrame(quint8 opcode, const QByteArray &payload);
	void fail(const QString &reason);

	QSslSocket m_socket;
	State m_state = State::Closed;
	QUrl m_url;
	Headers m_headers;
	QByteArray m_key;
	QByteArray m_buffer;
	QByteArray m_fragments;
	quint8 m_fragmentOpcode = 0;
};
