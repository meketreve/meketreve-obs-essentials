/*
Meketreve OBS Essentials - Shared configuration
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

#include "config-share.h"
#include "config-share.hpp"
#include "config-codec.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <vector>

namespace {

std::vector<ConfigSection> g_sections;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

struct Preset {
	QString name;
	QString description;
	QJsonObject bundle;
};

/* A preset's name/description is either a string or {"en-US": ..., "pt-BR": ...}. */
QString localized(const QJsonValue &v)
{
	if (v.isString())
		return v.toString();
	const QJsonObject o = v.toObject();
	const QString locale = QString::fromUtf8(obs_get_locale());
	if (o.contains(locale))
		return o.value(locale).toString();
	return o.value(QStringLiteral("en-US")).toString();
}

std::vector<Preset> loadPresets()
{
	std::vector<Preset> presets;
	char *dir = obs_module_file("presets");
	if (!dir)
		return presets;
	const QDir presetDir(QString::fromUtf8(dir));
	bfree(dir);

	for (const QString &file : presetDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
		QFile f(presetDir.filePath(file));
		if (!f.open(QIODevice::ReadOnly))
			continue;
		const QJsonObject bundle = QJsonDocument::fromJson(f.readAll()).object();
		QString error;
		if (!ConfigCodec::validate(bundle, &error)) {
			obs_log(LOG_WARNING, "[config] preset %s: %s", file.toUtf8().constData(),
				error.toUtf8().constData());
			continue;
		}
		presets.push_back({localized(bundle.value(QStringLiteral("name"))),
				   localized(bundle.value(QStringLiteral("description"))), bundle});
	}
	return presets;
}

QJsonObject exportBundle(const QString &name, const std::vector<bool> &include)
{
	QJsonObject bundle{{QStringLiteral("format"), ConfigCodec::kFormat}};
	if (!name.trimmed().isEmpty())
		bundle.insert(QStringLiteral("name"), name.trimmed());
	for (size_t i = 0; i < g_sections.size(); i++) {
		if (include[i])
			bundle.insert(g_sections[i].key, g_sections[i].save());
	}
	return bundle;
}

void openExportDialog()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QDialog dialog(main);
	dialog.setWindowTitle(T("Config.ExportTitle"));
	dialog.setMinimumWidth(520);
	auto *layout = new QVBoxLayout(&dialog);

	auto *intro = new QLabel(T("Config.ExportIntro"), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *form = new QFormLayout();
	auto *name = new QLineEdit(&dialog);
	name->setPlaceholderText(T("Config.NamePlaceholder"));
	form->addRow(T("Config.Name"), name);
	layout->addLayout(form);

	std::vector<QCheckBox *> boxes;
	std::vector<QJsonValue> values;
	for (const ConfigSection &s : g_sections) {
		values.push_back(s.save());
		auto *box =
			new QCheckBox(QStringLiteral("%1 — %2").arg(T(s.labelKey), s.describe(values.back())), &dialog);
		box->setChecked(true);
		layout->addWidget(box);
		boxes.push_back(box);
	}

	auto *keysNote = new QLabel(T("Config.NoSecrets"), &dialog);
	keysNote->setWordWrap(true);
	layout->addWidget(keysNote);

	auto *text = new QPlainTextEdit(&dialog);
	text->setReadOnly(true);
	text->setMinimumHeight(110);
	layout->addWidget(text);

	const auto refresh = [&]() {
		std::vector<bool> include;
		for (QCheckBox *b : boxes)
			include.push_back(b->isChecked());
		text->setPlainText(ConfigCodec::encode(exportBundle(name->text(), include)));
	};
	for (QCheckBox *b : boxes)
		QObject::connect(b, &QCheckBox::toggled, &dialog, refresh);
	QObject::connect(name, &QLineEdit::textChanged, &dialog, refresh);
	refresh();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	QPushButton *copy = buttons->addButton(T("Config.Copy"), QDialogButtonBox::ActionRole);
	QObject::connect(copy, &QPushButton::clicked, &dialog, [text, copy]() {
		QApplication::clipboard()->setText(text->toPlainText());
		copy->setText(T("Config.Copied"));
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

void openImportDialog()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	const std::vector<Preset> presets = loadPresets();

	QDialog dialog(main);
	dialog.setWindowTitle(T("Config.ImportTitle"));
	dialog.setMinimumWidth(520);
	auto *layout = new QVBoxLayout(&dialog);

	auto *source = new QComboBox(&dialog);
	source->addItem(T("Config.PasteText"));
	for (const Preset &p : presets)
		source->addItem(T("Config.Preset").arg(p.name));
	layout->addWidget(source);

	auto *text = new QPlainTextEdit(&dialog);
	text->setPlaceholderText(T("Config.PastePlaceholder"));
	text->setMinimumHeight(90);
	layout->addWidget(text);

	auto *summary = new QLabel(&dialog);
	summary->setWordWrap(true);
	layout->addWidget(summary);

	auto *sectionsBox = new QWidget(&dialog);
	auto *sectionsLayout = new QVBoxLayout(sectionsBox);
	sectionsLayout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(sectionsBox);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
	QPushButton *apply = buttons->addButton(T("Config.Apply"), QDialogButtonBox::AcceptRole);
	apply->setEnabled(false);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	QJsonObject bundle;
	std::vector<std::pair<size_t, QCheckBox *>> boxes;

	const auto show = [&](const QJsonObject &b, const QString &error, const QString &description) {
		for (auto &entry : boxes)
			delete entry.second;
		boxes.clear();
		bundle = b;
		if (!error.isEmpty()) {
			summary->setText(QStringLiteral("<span style=\"color:#E03C3C\">%1</span>")
						 .arg(T("Config.Invalid").arg(error).toHtmlEscaped()));
			apply->setEnabled(false);
			return;
		}
		if (b.isEmpty()) {
			summary->clear();
			apply->setEnabled(false);
			return;
		}
		QString head = b.value(QStringLiteral("name")).toString();
		if (head.isEmpty())
			head = localized(b.value(QStringLiteral("name")));
		QString info =
			head.isEmpty()
				? T("Config.Contains")
				: QStringLiteral("<b>%1</b><br>%2").arg(head.toHtmlEscaped(), T("Config.Contains"));
		if (!description.isEmpty())
			info = QStringLiteral("%1<br>%2").arg(description.toHtmlEscaped(), info);
		summary->setText(info);
		for (size_t i = 0; i < g_sections.size(); i++) {
			const ConfigSection &s = g_sections[i];
			if (!b.contains(s.key))
				continue;
			auto *box = new QCheckBox(
				QStringLiteral("%1 — %2").arg(T(s.labelKey), s.describe(b.value(s.key))), sectionsBox);
			box->setChecked(true);
			sectionsLayout->addWidget(box);
			boxes.emplace_back(i, box);
		}
		if (boxes.empty())
			summary->setText(summary->text() + QStringLiteral("<br>") + T("Config.Nothing"));
		apply->setEnabled(!boxes.empty());
	};

	QObject::connect(text, &QPlainTextEdit::textChanged, &dialog, [&]() {
		if (source->currentIndex() != 0)
			return;
		const QString t = text->toPlainText().trimmed();
		if (t.isEmpty()) {
			show({}, {}, {});
			return;
		}
		QJsonObject b;
		QString error;
		if (ConfigCodec::decode(t, b, &error))
			show(b, {}, {});
		else
			show({}, error, {});
	});
	QObject::connect(source, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
		text->setEnabled(index == 0);
		if (index == 0) {
			show({}, {}, {});
			Q_EMIT text->textChanged();
			return;
		}
		const Preset &p = presets[static_cast<size_t>(index - 1)];
		QJsonObject b = p.bundle;
		b.insert(QStringLiteral("name"), p.name);
		show(b, {}, p.description);
	});

	if (dialog.exec() != QDialog::Accepted || bundle.isEmpty())
		return;

	QStringList applied;
	for (const auto &entry : boxes) {
		if (!entry.second->isChecked())
			continue;
		const ConfigSection &s = g_sections[entry.first];
		s.load(bundle.value(s.key));
		applied.append(s.key);
	}
	obs_log(LOG_INFO, "[config] imported: %s", applied.join(QStringLiteral(", ")).toUtf8().constData());
}

} // namespace

void configShareAddSection(const ConfigSection &section)
{
	for (ConfigSection &s : g_sections) {
		if (s.key == section.key) {
			s = section;
			return;
		}
	}
	g_sections.push_back(section);
}

void configShareOpenExport()
{
	openExportDialog();
}

void configShareOpenImport()
{
	openImportDialog();
}

void config_share_register(void)
{
	auto *exportAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Config.ExportMenu")));
	QObject::connect(exportAction, &QAction::triggered, openExportDialog);
	auto *importAction =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Config.ImportMenu")));
	QObject::connect(importAction, &QAction::triggered, openImportDialog);
}
