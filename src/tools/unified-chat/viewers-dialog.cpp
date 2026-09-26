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
#include "viewers-dialog.hpp"

#include <obs-module.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTime>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

constexpr int kIdRole = Qt::UserRole;
constexpr int kNameRole = Qt::UserRole + 1;

QString shownName(const ChatUser &u)
{
	return u.name.isEmpty() ? u.login : u.name;
}

void sortByName(QList<ChatUser> &users)
{
	std::sort(users.begin(), users.end(), [](const ChatUser &a, const ChatUser &b) {
		return QString::compare(shownName(a), shownName(b), Qt::CaseInsensitive) < 0;
	});
}

} // namespace

ViewersDialog::ViewersDialog(ChatAccounts *accounts, const QString &channel, QWidget *parent)
	: QDialog(parent),
	  m_accounts(accounts),
	  m_channel(channel)
{
	setWindowTitle(T("UnifiedChat.Viewers.Title"));
	resize(460, 560);
	auto *layout = new QVBoxLayout(this);

	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(T("UnifiedChat.Viewers.Filter"));
	m_filter->setClearButtonEnabled(true);
	connect(m_filter, &QLineEdit::textChanged, this, &ViewersDialog::applyFilter);
	layout->addWidget(m_filter);

	m_tabs = new QTabWidget(this);

	auto *chatPage = new QWidget(m_tabs);
	auto *chatLayout = new QVBoxLayout(chatPage);
	m_chatError = new QLabel(chatPage);
	m_chatError->setWordWrap(true);
	m_chatError->hide();
	m_chatters = new QListWidget(chatPage);
	m_chatters->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_chatters, &QListWidget::customContextMenuRequested, this, &ViewersDialog::chatterMenu);
	chatLayout->addWidget(m_chatError);
	chatLayout->addWidget(m_chatters, 1);
	m_tabs->addTab(chatPage, T("UnifiedChat.Viewers.InChat").arg(0));

	auto *banPage = new QWidget(m_tabs);
	auto *banLayout = new QVBoxLayout(banPage);
	m_banError = new QLabel(banPage);
	m_banError->setWordWrap(true);
	m_banError->hide();
	m_banned = new QTreeWidget(banPage);
	m_banned->setRootIsDecorated(false);
	m_banned->setHeaderLabels({T("UnifiedChat.Viewers.ColName"), T("UnifiedChat.Viewers.ColUntil"),
				   T("UnifiedChat.Viewers.ColReason"), T("UnifiedChat.Viewers.ColBy")});
	m_banned->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_banned->header()->setStretchLastSection(true);
	m_unban = new QPushButton(T("UnifiedChat.Unban"), banPage);
	m_unban->setEnabled(false);
	connect(m_banned, &QTreeWidget::itemSelectionChanged, this,
		[this]() { m_unban->setEnabled(!m_banned->selectedItems().isEmpty()); });
	connect(m_unban, &QPushButton::clicked, this, &ViewersDialog::unbanSelected);
	banLayout->addWidget(m_banError);
	banLayout->addWidget(m_banned, 1);
	banLayout->addWidget(m_unban, 0, Qt::AlignRight);
	m_tabs->addTab(banPage, T("UnifiedChat.Viewers.Banned").arg(0));
	layout->addWidget(m_tabs, 1);

	auto *note = new QLabel(T("UnifiedChat.Viewers.Note"), this);
	note->setWordWrap(true);
	note->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(note);

	auto *buttons = new QDialogButtonBox(this);
	m_refresh = buttons->addButton(T("UnifiedChat.Viewers.Refresh"), QDialogButtonBox::ActionRole);
	buttons->addButton(T("UnifiedChat.Viewers.Close"), QDialogButtonBox::RejectRole);
	connect(m_refresh, &QPushButton::clicked, this, &ViewersDialog::refresh);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	/* A failure before the request (unknown channel) never reaches the
	 * callbacks: stop waiting and show it. */
	connect(m_accounts, &ChatAccounts::actionFailed, this, [this](ChatPlatform p, const QString &error) {
		if (p != ChatPlatform::Twitch || m_pending == 0)
			return;
		m_pending = 0;
		setBusy(0);
		m_status->setText(T("UnifiedChat.Viewers.Error").arg(error));
	});
}

void ViewersDialog::setChannel(const QString &channel)
{
	m_channel = channel;
}

void ViewersDialog::setBusy(int delta)
{
	m_pending = std::max(0, m_pending + delta);
	m_refresh->setEnabled(m_pending == 0);
	if (m_pending > 0)
		m_status->setText(T("UnifiedChat.Viewers.Loading"));
	else if (m_status->text() == T("UnifiedChat.Viewers.Loading"))
		m_status->setText(T("UnifiedChat.Viewers.Updated")
					  .arg(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
}

void ViewersDialog::refresh()
{
	if (m_pending > 0)
		return;
	if (!m_accounts->account(ChatPlatform::Twitch).loggedIn()) {
		m_status->setText(T("UnifiedChat.Viewers.NotLoggedIn"));
		return;
	}
	if (m_channel.trimmed().isEmpty()) {
		m_status->setText(T("UnifiedChat.Viewers.NoChannel"));
		return;
	}
	setBusy(2);
	QPointer<ViewersDialog> self(this);
	m_accounts->twitchChatterList(m_channel, [self](const QList<ChatUser> &users, const QString &error) {
		if (!self)
			return;
		self->showChatters(users, error);
		self->setBusy(-1);
	});
	m_accounts->twitchBanList(m_channel, [self](const QList<ChatUser> &users, const QString &error) {
		if (!self)
			return;
		self->showBanned(users, error);
		self->setBusy(-1);
	});
}

void ViewersDialog::showChatters(const QList<ChatUser> &users, const QString &error)
{
	QList<ChatUser> sorted = users;
	sortByName(sorted);
	m_chatters->clear();
	for (const ChatUser &u : sorted) {
		auto *item = new QListWidgetItem(shownName(u), m_chatters);
		item->setData(kIdRole, u.id);
		item->setData(kNameRole, shownName(u));
	}
	m_chatError->setText(T("UnifiedChat.Viewers.Error").arg(error));
	m_chatError->setVisible(!error.isEmpty());
	m_tabs->setTabText(0, T("UnifiedChat.Viewers.InChat").arg(sorted.size()));
	applyFilter();
}

void ViewersDialog::showBanned(const QList<ChatUser> &users, const QString &error)
{
	QList<ChatUser> sorted = users;
	sortByName(sorted);
	m_banned->clear();
	for (const ChatUser &u : sorted) {
		const QString until = u.expiresAt.isValid()
					      ? QLocale().toString(u.expiresAt.toLocalTime(), QLocale::ShortFormat)
					      : T("UnifiedChat.Viewers.Permanent");
		auto *item = new QTreeWidgetItem(m_banned, {shownName(u), until, u.reason, u.moderator});
		item->setData(0, kIdRole, u.id);
		item->setData(0, kNameRole, shownName(u));
	}
	/* Twitch only gives the ban list to the broadcaster. */
	m_banError->setText(T("UnifiedChat.Viewers.BanListError").arg(error));
	m_banError->setVisible(!error.isEmpty());
	m_tabs->setTabText(1, T("UnifiedChat.Viewers.Banned").arg(sorted.size()));
	applyFilter();
}

void ViewersDialog::showBanTab()
{
	m_tabs->setCurrentIndex(1);
}

void ViewersDialog::applyFilter()
{
	const QString text = m_filter->text().trimmed();
	for (int i = 0; i < m_chatters->count(); i++) {
		QListWidgetItem *item = m_chatters->item(i);
		item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
	}
	for (int i = 0; i < m_banned->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = m_banned->topLevelItem(i);
		item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive));
	}
}

void ViewersDialog::chatterMenu(const QPoint &pos)
{
	QListWidgetItem *item = m_chatters->itemAt(pos);
	if (!item)
		return;
	const QString id = item->data(kIdRole).toString();
	const QString name = item->data(kNameRole).toString();
	const auto done = [self = QPointer<ViewersDialog>(this)](const QString &error) {
		if (self)
			self->afterAction(error);
	};
	QMenu menu(this);
	menu.addSection(name);
	menu.addAction(T("UnifiedChat.Timeout60"), this,
		       [this, id, done]() { m_accounts->timeoutUser(ChatPlatform::Twitch, m_channel, id, 60, done); });
	menu.addAction(T("UnifiedChat.Timeout600"), this,
		       [this, id, done]() { m_accounts->timeoutUser(ChatPlatform::Twitch, m_channel, id, 600, done); });
	menu.addAction(T("UnifiedChat.Ban"), this, [this, id, name, done]() {
		if (QMessageBox::question(this, T("UnifiedChat.Ban"), T("UnifiedChat.BanConfirm").arg(name)) ==
		    QMessageBox::Yes)
			m_accounts->banUser(ChatPlatform::Twitch, m_channel, id, done);
	});
	menu.exec(m_chatters->viewport()->mapToGlobal(pos));
}

void ViewersDialog::unbanSelected()
{
	const QList<QTreeWidgetItem *> items = m_banned->selectedItems();
	if (items.isEmpty())
		return;
	const QString id = items.first()->data(0, kIdRole).toString();
	m_accounts->unbanUser(ChatPlatform::Twitch, m_channel, id,
			      [self = QPointer<ViewersDialog>(this)](const QString &error) {
				      if (self)
					      self->afterAction(error);
			      });
}

void ViewersDialog::afterAction(const QString &error)
{
	if (!error.isEmpty()) {
		m_status->setText(T("UnifiedChat.Viewers.Error").arg(error));
		return;
	}
	refresh();
}
