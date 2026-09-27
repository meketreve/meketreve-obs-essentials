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

#include <QList>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/* One title and one category for the stream, sent to every logged-in
 * platform at once. Each platform is a row in m_targets, so another one
 * only needs its API in ChatAccounts and an entry there. */
class StreamInfoDock : public QWidget {
	Q_OBJECT

public:
	explicit StreamInfoDock(ChatAccounts *accounts, QWidget *parent = nullptr);

	void reload();

private:
	struct Target {
		ChatPlatform platform;
		const char *name;
		QCheckBox *enabled = nullptr;
		QLabel *status = nullptr;
		QList<StreamCategory> results; /* last search on this platform */
		StreamCategory chosen;         /* empty id = leave the category as it is */
	};

	void updateRows();
	void search();
	void pickCategory(const QString name); /* a copy: it outlives the list item */
	void apply();
	bool active(const Target &t) const;
	void setStatus(Target &t, const QString &text, const char *color = nullptr);

	ChatAccounts *m_accounts;
	QList<Target> m_targets;
	QLineEdit *m_title = nullptr;
	QLineEdit *m_search = nullptr;
	QListWidget *m_results = nullptr;
	QLabel *m_chosen = nullptr;
	QPushButton *m_apply = nullptr;
	QString m_chosenName;
	QTimer m_searchDelay;
	int m_pending = 0;
};
