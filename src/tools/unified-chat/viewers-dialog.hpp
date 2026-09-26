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

#include "chat-accounts.hpp"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTreeWidget;

/* Who is in the Twitch chat and who is banned there, with moderation
 * actions. Only Twitch offers these lists. */
class ViewersDialog : public QDialog {
	Q_OBJECT

public:
	ViewersDialog(ChatAccounts *accounts, const QString &channel, QWidget *parent = nullptr);

	void setChannel(const QString &channel);
	void refresh();
	/* Shows the given lists (used by refresh and by the dock harness). */
	void showChatters(const QList<ChatUser> &users, const QString &error);
	void showBanned(const QList<ChatUser> &users, const QString &error);
	void showBanTab();

private:
	void applyFilter();
	void setBusy(int delta);
	void chatterMenu(const QPoint &pos);
	void unbanSelected();
	void afterAction(const QString &error);

	ChatAccounts *m_accounts;
	QString m_channel;
	int m_pending = 0;
	QLabel *m_status = nullptr;
	QLineEdit *m_filter = nullptr;
	QTabWidget *m_tabs = nullptr;
	QLabel *m_chatError = nullptr;
	QListWidget *m_chatters = nullptr;
	QLabel *m_banError = nullptr;
	QTreeWidget *m_banned = nullptr;
	QPushButton *m_unban = nullptr;
	QPushButton *m_refresh = nullptr;
};
