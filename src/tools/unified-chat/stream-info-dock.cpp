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
#include "stream-info-dock.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Twitch's limit, and Kick's is higher. */
constexpr int kMaxTitle = 140;

bool looksLikeMissingPermission(const QString &error)
{
	for (const char *hint : {"scope", "401", "403", "unauthorized", "forbidden", "permission"}) {
		if (error.contains(QLatin1String(hint), Qt::CaseInsensitive))
			return true;
	}
	return false;
}

StreamCategory exactMatch(const QList<StreamCategory> &list, const QString &name)
{
	for (const StreamCategory &c : list) {
		if (c.name.compare(name, Qt::CaseInsensitive) == 0)
			return c;
	}
	return StreamCategory();
}

} // namespace

StreamInfoDock::StreamInfoDock(ChatAccounts *accounts, QWidget *parent) : QWidget(parent), m_accounts(accounts)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);

	auto *form = new QFormLayout();
	m_title = new QLineEdit(this);
	m_title->setPlaceholderText(T("StreamInfo.TitlePlaceholder"));
	m_title->setMaxLength(kMaxTitle);
	m_title->setClearButtonEnabled(true);
	form->addRow(T("StreamInfo.StreamTitle"), m_title);

	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(T("StreamInfo.SearchPlaceholder"));
	m_search->setClearButtonEnabled(true);
	form->addRow(T("StreamInfo.Category"), m_search);
	layout->addLayout(form);

	m_results = new QListWidget(this);
	m_results->setMaximumHeight(130);
	m_results->hide();
	layout->addWidget(m_results);

	m_chosen = new QLabel(T("StreamInfo.CategoryUnchanged"), this);
	m_chosen->setWordWrap(true);
	layout->addWidget(m_chosen);

	auto *applyTo = new QLabel(QStringLiteral("<b>%1</b>").arg(T("StreamInfo.ApplyTo").toHtmlEscaped()), this);
	layout->addWidget(applyTo);
	m_targets = {{ChatPlatform::Twitch, "Twitch"},
		     {ChatPlatform::YouTube, "YouTube"},
		     {ChatPlatform::Kick, "Kick"},
		     {ChatPlatform::Trovo, "Trovo"}};
	for (Target &t : m_targets) {
		auto *row = new QHBoxLayout();
		t.enabled = new QCheckBox(QString::fromLatin1(t.name), this);
		t.enabled->setChecked(true);
		t.status = new QLabel(this);
		t.status->setWordWrap(true);
		t.status->setTextInteractionFlags(Qt::TextSelectableByMouse);
		row->addWidget(t.enabled);
		row->addWidget(t.status, 1);
		layout->addLayout(row);
	}

	auto *note = new QLabel(T("StreamInfo.Note"), this);
	note->setWordWrap(true);
	note->setStyleSheet(QStringLiteral("color: gray"));
	layout->addWidget(note);

	auto *buttons = new QHBoxLayout();
	auto *reloadButton = new QPushButton(T("StreamInfo.Reload"), this);
	m_apply = new QPushButton(T("StreamInfo.Apply"), this);
	m_apply->setDefault(true);
	buttons->addWidget(reloadButton);
	buttons->addStretch();
	buttons->addWidget(m_apply);
	layout->addLayout(buttons);
	layout->addStretch();

	m_searchDelay.setSingleShot(true);
	m_searchDelay.setInterval(400);
	connect(&m_searchDelay, &QTimer::timeout, this, &StreamInfoDock::search);
	connect(m_search, &QLineEdit::textEdited, this, [this]() { m_searchDelay.start(); });
	connect(m_results, &QListWidget::itemClicked, this,
		[this](QListWidgetItem *item) { pickCategory(item->text()); });
	connect(reloadButton, &QPushButton::clicked, this, &StreamInfoDock::reload);
	connect(m_apply, &QPushButton::clicked, this, &StreamInfoDock::apply);
	connect(m_accounts, &ChatAccounts::accountChanged, this, [this]() {
		updateRows();
		reload();
	});

	updateRows();
	reload();
}

bool StreamInfoDock::active(const Target &t) const
{
	return m_accounts->account(t.platform).loggedIn() && t.enabled->isChecked();
}

void StreamInfoDock::setStatus(Target &t, const QString &text, const char *color)
{
	t.status->setText(color ? QStringLiteral("<span style=\"color:%1\">%2</span>")
					  .arg(QLatin1String(color), text.toHtmlEscaped())
				: text.toHtmlEscaped());
}

void StreamInfoDock::updateRows()
{
	for (Target &t : m_targets) {
		const bool loggedIn = m_accounts->account(t.platform).loggedIn();
		t.enabled->setEnabled(loggedIn);
		if (!loggedIn)
			setStatus(t, T("StreamInfo.NotLoggedIn"), "gray");
	}
}

void StreamInfoDock::reload()
{
	for (int i = 0; i < m_targets.size(); i++) {
		Target &t = m_targets[i];
		if (!m_accounts->account(t.platform).loggedIn())
			continue;
		setStatus(t, T("StreamInfo.Loading"), "gray");
		QPointer<StreamInfoDock> self(this);
		m_accounts->streamInfo(t.platform, [self, i](const StreamInfo &info, const QString &error) {
			if (!self)
				return;
			Target &t = self->m_targets[i];
			if (!error.isEmpty()) {
				self->setStatus(t, T("StreamInfo.Failed").arg(error), "#E03C3C");
				return;
			}
			self->setStatus(t, T("StreamInfo.Current")
						   .arg(info.title.isEmpty() ? QStringLiteral("—") : info.title,
							info.categoryName.isEmpty() ? QStringLiteral("—")
										    : info.categoryName));
			/* Start from what is live, but never overwrite what you typed. */
			if (self->m_title->text().isEmpty())
				self->m_title->setText(info.title);
		});
	}
}

void StreamInfoDock::search()
{
	const QString query = m_search->text().trimmed();
	for (Target &t : m_targets)
		t.results.clear();
	m_results->clear();
	if (query.size() < 2) {
		m_results->hide();
		return;
	}
	for (int i = 0; i < m_targets.size(); i++) {
		if (!active(m_targets[i]))
			continue;
		QPointer<StreamInfoDock> self(this);
		m_accounts->searchCategories(m_targets[i].platform, query,
					     [self, i, query](const QList<StreamCategory> &found, const QString &) {
						     if (!self || self->m_search->text().trimmed() != query)
							     return;
						     self->m_targets[i].results = found;
						     /* One list for every platform: the same name shows once. */
						     QSet<QString> shown;
						     for (int r = 0; r < self->m_results->count(); r++)
							     shown.insert(self->m_results->item(r)->text().toLower());
						     for (const StreamCategory &c : found) {
							     if (!shown.contains(c.name.toLower())) {
								     shown.insert(c.name.toLower());
								     self->m_results->addItem(c.name);
							     }
						     }
						     self->m_results->setVisible(self->m_results->count() > 0);
					     });
	}
}

void StreamInfoDock::pickCategory(const QString name)
{
	m_chosenName = name;
	m_chosen->setText(T("StreamInfo.CategoryChosen").arg(name));
	m_results->hide();
	m_search->clear();
	for (int i = 0; i < m_targets.size(); i++) {
		Target &t = m_targets[i];
		t.chosen = StreamCategory();
		if (!active(t))
			continue;
		t.chosen = exactMatch(t.results, name);
		if (!t.chosen.id.isEmpty())
			continue;
		/* Picked from another platform's results: look the name up here. */
		QPointer<StreamInfoDock> self(this);
		m_accounts->searchCategories(
			t.platform, name, [self, i, name](const QList<StreamCategory> &found, const QString &) {
				if (!self || self->m_chosenName != name)
					return;
				Target &t = self->m_targets[i];
				t.chosen = exactMatch(found, name);
				if (t.chosen.id.isEmpty())
					self->setStatus(t, T("StreamInfo.NoMatch").arg(name), "#E0A000");
			});
	}
}

void StreamInfoDock::apply()
{
	const QString title = m_title->text().trimmed();
	if (title.isEmpty() && m_chosenName.isEmpty()) {
		for (Target &t : m_targets) {
			if (active(t))
				setStatus(t, T("StreamInfo.Nothing"), "#E0A000");
		}
		return;
	}
	for (int i = 0; i < m_targets.size(); i++) {
		Target &t = m_targets[i];
		if (!active(t))
			continue;
		setStatus(t, T("StreamInfo.Updating"), "gray");
		m_pending++;
		m_apply->setEnabled(false);
		QPointer<StreamInfoDock> self(this);
		m_accounts->updateStreamInfo(t.platform, title, t.chosen.id, [self, i](const QString &error) {
			if (!self)
				return;
			Target &t = self->m_targets[i];
			if (error.isEmpty()) {
				self->setStatus(t, T("StreamInfo.Updated"), "#2B8A3E");
			} else {
				QString text = T("StreamInfo.Failed").arg(error);
				if (looksLikeMissingPermission(error))
					text += QLatin1Char(' ') + T("StreamInfo.Relogin");
				self->setStatus(t, text, "#E03C3C");
			}
			if (--self->m_pending == 0)
				self->m_apply->setEnabled(true);
		});
	}
}
