/*
Meketreve OBS Essentials - Languages
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

#include "i18n.h"
#include "i18n.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString languageName(const QString &lang)
{
	if (lang == QLatin1String("pt"))
		return QStringLiteral("Português");
	if (lang == QLatin1String("both"))
		return T("Language.Both");
	return QStringLiteral("English");
}

QComboBox *combo(const QList<QPair<QString, QString>> &items, const QString &current)
{
	auto *box = new QComboBox;
	for (const auto &item : items)
		box->addItem(item.second, item.first);
	box->setCurrentIndex(qMax(0, box->findData(current)));
	return box;
}

QLabel *note(const QString &text)
{
	auto *label = new QLabel(text);
	label->setWordWrap(true);
	label->setStyleSheet(QStringLiteral("color: gray"));
	return label;
}

} // namespace

void I18n::openDialog()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	const I18n::Settings saved = I18n::saved();

	QDialog dialog(main);
	dialog.setWindowTitle(T("Language.Title"));
	dialog.setMinimumWidth(460);
	auto *layout = new QVBoxLayout(&dialog);

	auto *ui = combo({{QStringLiteral("auto"), T("Language.Auto")},
			  {QStringLiteral("pt"), QStringLiteral("Português")},
			  {QStringLiteral("en"), QStringLiteral("English")}},
			 saved.ui);
	auto *stream = combo({{QStringLiteral("plugin"), T("Language.FollowPlugin")},
			      {QStringLiteral("pt"), QStringLiteral("Português")},
			      {QStringLiteral("en"), QStringLiteral("English")},
			      {QStringLiteral("both"), T("Language.Both")}},
			     saved.stream);

	auto *form = new QFormLayout;
	form->addRow(T("Language.Plugin"), ui);
	form->addRow(QString(), note(T("Language.PluginHelp")));
	form->addRow(T("Language.Stream"), stream);
	form->addRow(QString(), note(T("Language.StreamHelp")));
	layout->addLayout(form);

	layout->addWidget(note(T("Language.Now").arg(languageName(I18n::plugin()), languageName(I18n::stream()))));
	auto *restart = new QLabel(T("Language.Restart"));
	restart->setWordWrap(true);
	layout->addWidget(restart);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Save)->setText(T("Language.Save"));
	buttons->button(QDialogButtonBox::Cancel)->setText(T("Language.Cancel"));
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return;
	I18n::Settings chosen;
	chosen.ui = ui->currentData().toString();
	chosen.stream = stream->currentData().toString();
	if (chosen.ui == saved.ui && chosen.stream == saved.stream)
		return;
	if (!I18n::save(chosen)) {
		QMessageBox::warning(main, T("Language.Title"), T("Language.SaveFailed"));
		return;
	}
	QMessageBox::information(main, T("Language.Title"), T("Language.Saved"));
}

void language_register(void)
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Language.Menu")));
	QObject::connect(action, &QAction::triggered, I18n::openDialog);
}
