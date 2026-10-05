/*
Meketreve OBS Essentials - Updater
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
#include "updater.h"
#include "update-logic.hpp"
#include "../../i18n/i18n.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

using UpdateLogic::Package;
using UpdateLogic::Release;

namespace {

constexpr const char *kSettingsFile = "updater.json";
/* Give OBS time to settle (and the network to come up) before asking. */
constexpr int kStartupDelayMs = 15000;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QWidget *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

/* ---- settings: check on start, skipped version, update waiting for exit ---- */

struct Settings {
	bool checkOnStart = true;
	QString skipped;
	QString pending; /* version downloaded, to install when OBS closes */
};

QString settingsPath()
{
	char *dir = obs_module_config_path("");
	QDir().mkpath(QString::fromUtf8(dir));
	bfree(dir);
	char *file = obs_module_config_path(kSettingsFile);
	const QString path = QString::fromUtf8(file);
	bfree(file);
	return path;
}

Settings loadSettings()
{
	Settings s;
	obs_data_t *data = obs_data_create_from_json_file_safe(settingsPath().toUtf8().constData(), "bak");
	if (!data)
		return s;
	obs_data_set_default_bool(data, "checkOnStart", true);
	s.checkOnStart = obs_data_get_bool(data, "checkOnStart");
	s.skipped = QString::fromUtf8(obs_data_get_string(data, "skipped"));
	s.pending = QString::fromUtf8(obs_data_get_string(data, "pending"));
	obs_data_release(data);
	return s;
}

void saveSettings(const Settings &s)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "checkOnStart", s.checkOnStart);
	obs_data_set_string(data, "skipped", s.skipped.toUtf8().constData());
	obs_data_set_string(data, "pending", s.pending.toUtf8().constData());
	obs_data_save_json_safe(data, settingsPath().toUtf8().constData(), "tmp", "bak");
	obs_data_release(data);
}

/* ---- how this copy of the plugin can update itself ---- */

enum class Method {
	WindowsInstaller, /* run the Inno Setup installer silently */
	DebUserCopy,      /* unpack the .deb into the plugin folder in $HOME */
	DebSystem,        /* pkexec apt-get install (plugin under /usr) */
	MacPkg,           /* open the .pkg in Installer */
	Manual,           /* only the release page */
};

struct Plan {
	Method method = Method::Manual;
	Package package = Package::WindowsInstaller;
	QString reason; /* why Manual */
};

Plan installPlan()
{
	Plan plan;
#if defined(Q_OS_WIN)
	plan.package = Package::WindowsInstaller;
	/* The installer writes to %ProgramData%\obs-studio\plugins; a copy
	 * somewhere else would end up installed twice. */
	const QString binary =
		QDir::fromNativeSeparators(QString::fromUtf8(obs_get_module_binary_path(obs_current_module())));
	const QString programData = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramData"));
	if (!programData.isEmpty() &&
	    binary.startsWith(programData + QStringLiteral("/obs-studio/plugins/"), Qt::CaseInsensitive))
		plan.method = Method::WindowsInstaller;
	else
		plan.reason = T("Updater.ReasonOtherFolder");
#elif defined(Q_OS_MACOS)
	plan.method = Method::MacPkg;
	plan.package = Package::MacPkg;
#else
	plan.package = Package::Deb;
	const QString binary = QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
	if (binary.contains(QLatin1String("/.var/app/")) || !qEnvironmentVariableIsEmpty("FLATPAK_ID")) {
		plan.reason = T("Updater.ReasonFlatpak");
	} else if (QStandardPaths::findExecutable(QStringLiteral("dpkg-deb")).isEmpty()) {
		plan.reason = T("Updater.ReasonNoDpkg");
	} else if (binary.startsWith(QDir::homePath() + QLatin1Char('/'))) {
		plan.method = Method::DebUserCopy;
	} else if (!QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
		plan.method = Method::DebSystem;
	} else {
		plan.reason = T("Updater.ReasonNoPkexec");
	}
#endif
	return plan;
}

/* ---- state ---- */

QNetworkAccessManager *g_net = nullptr;
QString g_readyFile; /* downloaded and checked, installed at exit */
Method g_readyMethod = Method::Manual;
bool g_checking = false;

void openPage(const Release &release)
{
	QDesktopServices::openUrl(
		release.page.isValid()
			? release.page
			: QUrl(QStringLiteral("https://github.com/meketreve/meketreve-obs-essentials/releases/latest")));
}

/* Starts the install as a separate process that waits for OBS to exit, so
 * the plugin's files are no longer in use. */
void launchInstall()
{
	if (g_readyFile.isEmpty())
		return;
	const QString pid = QString::number(QCoreApplication::applicationPid());
	bool started = false;
	switch (g_readyMethod) {
	case Method::WindowsInstaller: {
		QString file = g_readyFile;
		file.replace(QLatin1Char('\''), QStringLiteral("''"));
		const QString script =
			QStringLiteral(
				"Wait-Process -Id %1 -ErrorAction SilentlyContinue; "
				"Start-Process -FilePath '%2' -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'")
				.arg(pid, file);
		started = QProcess::startDetached(QStringLiteral("powershell.exe"),
						  {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
						   QStringLiteral("Hidden"), QStringLiteral("-Command"), script});
		break;
	}
	case Method::DebUserCopy: {
		/* $1 pid, $2 .deb, $3 the .so to replace, $4 the data folder. */
		const QString script =
			QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; "
				       "t=$(mktemp -d) && dpkg-deb -x \"$2\" \"$t\" && "
				       "cp -f \"$t\"/usr/lib/*/obs-plugins/meketreve-obs-essentials.so \"$3\" && "
				       "cp -rf \"$t\"/usr/share/obs/obs-plugins/meketreve-obs-essentials/. \"$4\"/; "
				       "rm -rf \"$t\"");
		started = QProcess::startDetached(QStringLiteral("/bin/sh"),
						  {QStringLiteral("-c"), script, QStringLiteral("sh"), pid, g_readyFile,
						   QString::fromUtf8(obs_get_module_binary_path(obs_current_module())),
						   QString::fromUtf8(obs_get_module_data_path(obs_current_module()))});
		break;
	}
	case Method::DebSystem: {
		const QString script = QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; "
						      "pkexec apt-get install -y \"$2\"");
		started = QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script,
									      QStringLiteral("sh"), pid, g_readyFile});
		break;
	}
	case Method::MacPkg: {
		const QString script =
			QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; open \"$2\"");
		started = QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script,
									      QStringLiteral("sh"), pid, g_readyFile});
		break;
	}
	case Method::Manual:
		break;
	}
	obs_log(started ? LOG_INFO : LOG_WARNING, "[updater] %s the installer for %s",
		started ? "started" : "could not start", g_readyFile.toUtf8().constData());
}

void download(const Release &release, const Plan &plan)
{
	const QString asset = UpdateLogic::pickAsset(release, plan.package);
	if (asset.isEmpty()) {
		QMessageBox::information(mainWindow(), T("Updater.Title"),
					 T("Updater.Manual").arg(T("Updater.ReasonNoAsset")));
		openPage(release);
		return;
	}
	/* Only install what the release workflow vouched for. */
	const QString expected = release.checksums.value(asset);
	if (expected.isEmpty()) {
		QMessageBox::warning(mainWindow(), T("Updater.Title"), T("Updater.ChecksumFailed"));
		return;
	}

	auto *progress = new QProgressDialog(T("Updater.Downloading").arg(release.version), T("Updater.Later"), 0, 100,
					     mainWindow());
	progress->setWindowTitle(T("Updater.Title"));
	progress->setAttribute(Qt::WA_DeleteOnClose);
	progress->setMinimumDuration(0);
	progress->show();

	QNetworkRequest req(release.assets.value(asset));
	req.setHeader(QNetworkRequest::UserAgentHeader,
		      QStringLiteral("meketreve-obs-essentials/%1").arg(QString::fromUtf8(PLUGIN_VERSION)));
	req.setTransferTimeout(120000);
	QNetworkReply *reply = g_net->get(req);
	QObject::connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
	QObject::connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 got, qint64 total) {
		if (total > 0)
			progress->setValue(static_cast<int>(got * 100 / total));
	});
	QObject::connect(
		reply, &QNetworkReply::finished, mainWindow(),
		[reply, progress = QPointer<QProgressDialog>(progress), release, plan, asset, expected]() {
			reply->deleteLater();
			if (progress)
				progress->close();
			if (reply->error() == QNetworkReply::OperationCanceledError)
				return;
			if (reply->error() != QNetworkReply::NoError) {
				QMessageBox::warning(mainWindow(), T("Updater.Title"),
						     T("Updater.DownloadFailed").arg(reply->errorString()));
				return;
			}
			const QByteArray data = reply->readAll();
			const QString sum =
				QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
			if (sum != expected) {
				obs_log(LOG_WARNING, "[updater] checksum mismatch for %s", asset.toUtf8().constData());
				QMessageBox::warning(mainWindow(), T("Updater.Title"), T("Updater.ChecksumFailed"));
				return;
			}
			const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
						    .filePath(QStringLiteral("meketreve-obs-essentials-update"));
			QDir().mkpath(dir);
			const QString path = QDir(dir).filePath(asset);
			QFile file(path);
			if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
				QMessageBox::warning(mainWindow(), T("Updater.Title"),
						     T("Updater.DownloadFailed").arg(file.errorString()));
				return;
			}
			file.close();
			g_readyFile = path;
			g_readyMethod = plan.method;
			Settings s = loadSettings();
			s.pending = release.version;
			saveSettings(s);
			obs_log(LOG_INFO, "[updater] %s downloaded, installing when OBS closes",
				release.version.toUtf8().constData());
			QMessageBox::information(mainWindow(), T("Updater.Title"),
						 (plan.method == Method::DebSystem ? T("Updater.ReadySystem")
										   : T("Updater.Ready"))
							 .arg(release.version));
		});
}

void showAvailable(const Release &release)
{
	QDialog dialog(mainWindow());
	dialog.setWindowTitle(T("Updater.Title"));
	dialog.resize(520, 420);
	auto *layout = new QVBoxLayout(&dialog);
	auto *head = new QLabel(
		T("Updater.Available").arg(release.version.toHtmlEscaped(), QString::fromUtf8(PLUGIN_VERSION)),
		&dialog);
	head->setWordWrap(true);
	layout->addWidget(head);
	auto *notes = new QTextBrowser(&dialog);
	notes->setOpenExternalLinks(true);
	notes->setMarkdown(UpdateLogic::notesFor(release.notes, I18n::plugin()));
	layout->addWidget(notes, 1);

	const Plan plan = installPlan();
	if (plan.method == Method::Manual) {
		auto *manual = new QLabel(T("Updater.Manual").arg(plan.reason), &dialog);
		manual->setWordWrap(true);
		layout->addWidget(manual);
	}

	Settings settings = loadSettings();
	auto *onStart = new QCheckBox(T("Updater.CheckOnStart"), &dialog);
	onStart->setChecked(settings.checkOnStart);
	layout->addWidget(onStart);

	auto *buttons = new QDialogButtonBox(&dialog);
	QPushButton *update =
		buttons->addButton(plan.method == Method::Manual ? T("Updater.OpenPage") : T("Updater.Update"),
				   QDialogButtonBox::AcceptRole);
	update->setDefault(true);
	QPushButton *skip = buttons->addButton(T("Updater.Skip"), QDialogButtonBox::DestructiveRole);
	buttons->addButton(T("Updater.Later"), QDialogButtonBox::RejectRole);
	bool skipped = false;
	QObject::connect(skip, &QPushButton::clicked, &dialog, [&dialog, &skipped]() {
		skipped = true;
		dialog.reject();
	});
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	const bool accepted = dialog.exec() == QDialog::Accepted;
	settings.checkOnStart = onStart->isChecked();
	if (skipped)
		settings.skipped = release.version;
	saveSettings(settings);
	if (!accepted)
		return;
	if (plan.method == Method::Manual)
		openPage(release);
	else
		download(release, plan);
}

/* manual: from the Tools menu, so say something even when up to date. */
void check(bool manual)
{
	if (g_checking)
		return;
	if (!g_readyFile.isEmpty()) {
		if (manual)
			QMessageBox::information(mainWindow(), T("Updater.Title"),
						 T("Updater.Ready").arg(loadSettings().pending));
		return;
	}
	g_checking = true;
	QNetworkRequest req(QUrl(QString::fromLatin1(UpdateLogic::kLatestReleaseApi)));
	req.setHeader(QNetworkRequest::UserAgentHeader,
		      QStringLiteral("meketreve-obs-essentials/%1").arg(QString::fromUtf8(PLUGIN_VERSION)));
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setTransferTimeout(20000);
	QNetworkReply *reply = g_net->get(req);
	QObject::connect(reply, &QNetworkReply::finished, mainWindow(), [reply, manual]() {
		reply->deleteLater();
		g_checking = false;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || status != 200) {
			/* 404: no published release yet. */
			obs_log(LOG_INFO, "[updater] check failed: HTTP %d %s", status,
				reply->errorString().toUtf8().constData());
			if (manual)
				QMessageBox::warning(mainWindow(), T("Updater.Title"),
						     T("Updater.CheckFailed").arg(reply->errorString()));
			return;
		}
		const Release release = UpdateLogic::parseRelease(QJsonDocument::fromJson(reply->readAll()).object());
		const QString current = QString::fromUtf8(PLUGIN_VERSION);
		if (!UpdateLogic::isNewer(release.version, current)) {
			obs_log(LOG_INFO, "[updater] up to date (%s, latest %s)", PLUGIN_VERSION,
				release.version.toUtf8().constData());
			if (manual) {
				/* The only other place to turn the startup check off. */
				QMessageBox box(QMessageBox::Information, T("Updater.Title"),
						T("Updater.UpToDate").arg(current), QMessageBox::Ok, mainWindow());
				Settings settings = loadSettings();
				auto *onStart = new QCheckBox(T("Updater.CheckOnStart"), &box);
				onStart->setChecked(settings.checkOnStart);
				box.setCheckBox(onStart);
				box.exec();
				settings.checkOnStart = onStart->isChecked();
				saveSettings(settings);
			}
			return;
		}
		if (!manual && loadSettings().skipped == release.version) {
			obs_log(LOG_INFO, "[updater] %s available but skipped", release.version.toUtf8().constData());
			return;
		}
		showAvailable(release);
	});
}

/* An update was downloaded last session: did it land? */
void reportPending()
{
	Settings s = loadSettings();
	if (s.pending.isEmpty())
		return;
	const QString pending = s.pending;
	s.pending.clear();
	saveSettings(s);
	if (UpdateLogic::compareVersions(QString::fromUtf8(PLUGIN_VERSION), pending) >= 0) {
		obs_log(LOG_INFO, "[updater] updated to %s", PLUGIN_VERSION);
		return;
	}
	obs_log(LOG_WARNING, "[updater] the update to %s was not installed", pending.toUtf8().constData());
	QMessageBox::warning(mainWindow(), T("Updater.Title"), T("Updater.NotInstalled").arg(pending));
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		QTimer::singleShot(kStartupDelayMs, mainWindow(), []() {
			reportPending();
			if (loadSettings().checkOnStart)
				check(false);
		});
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		launchInstall();
	}
}

} // namespace

void updater_register(void)
{
	g_net = new QNetworkAccessManager(mainWindow());
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Updater.Menu")));
	QObject::connect(action, &QAction::triggered, []() { check(true); });
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
}
