/*
Meketreve OBS Essentials - Face mask
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
#include "face-mask-setup.hpp"
#include "face-mask-components.hpp"
#include "ort-loader.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace FaceMask::Setup {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_ready{false};
std::atomic<int> g_generation{0};
bool g_downloading = false;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString folder()
{
	char *path = obs_module_config_path("face-mask");
	const QString dir = QString::fromUtf8(path ? path : "");
	bfree(path);
	return dir;
}

QString pathOf(const Component &c)
{
	return QDir(folder()).filePath((c.model ? QStringLiteral("models/") : QString()) + QString::fromLatin1(c.file));
}

std::vector<Component> missing()
{
	std::vector<Component> list;
	for (const Component &c : components()) {
		if (!fileMatches(pathOf(c), c.size, QString::fromLatin1(c.sha256)))
			list.push_back(c);
	}
	return list;
}

/* One download after the other: each file goes to <name>.part and only
 * takes its real name once its size and SHA-256 are right. */
struct Job {
	std::vector<Component> files;
	size_t index = 0;
	qint64 done = 0;
	qint64 total = 0;
	QNetworkAccessManager *net = nullptr;
	QPointer<QProgressDialog> progress;
};

void finish(const std::shared_ptr<Job> &job, const QString &error, bool canceled = false)
{
	g_downloading = false;
	if (job->progress)
		job->progress->close();
	job->net->deleteLater();
	if (canceled) {
		obs_log(LOG_INFO, "[face-mask] download canceled");
		return;
	}
	if (!error.isEmpty()) {
		obs_log(LOG_WARNING, "[face-mask] download failed: %s", error.toUtf8().constData());
		if (!!qEnvironmentVariableIsEmpty("MEKETREVE_SELFTEST_FACEMASK"))
			QMessageBox::warning(static_cast<QMainWindow *>(obs_frontend_get_main_window()),
					     T("FaceMask.Name"), error);
		return;
	}
	const bool ok = check();
	obs_log(LOG_INFO, "[face-mask] components downloaded, ready=%d", ok);
}

void next(const std::shared_ptr<Job> &job)
{
	if (job->index >= job->files.size()) {
		finish(job, QString());
		return;
	}
	const Component c = job->files[job->index];
	const QString target = pathOf(c);
	QDir().mkpath(QFileInfo(target).path());
	auto part = std::make_shared<QFile>(target + QStringLiteral(".part"));
	if (!part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		finish(job, T("FaceMask.DownloadFailed").arg(QString::fromLatin1(c.file), part->errorString()));
		return;
	}

	QNetworkRequest req(QUrl(componentUrl(c)));
	req.setHeader(QNetworkRequest::UserAgentHeader,
		      QStringLiteral("meketreve-obs-essentials/%1").arg(QString::fromUtf8(PLUGIN_VERSION)));
	req.setTransferTimeout(60000);
	QNetworkReply *reply = job->net->get(req);
	if (job->progress)
		QObject::connect(job->progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
	auto received = std::make_shared<qint64>(0);
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, part, received, job, c]() {
		const QByteArray chunk = reply->readAll();
		*received += chunk.size();
		/* Never more than the file should have. */
		if (*received > c.size) {
			reply->abort();
			return;
		}
		part->write(chunk);
		if (job->progress)
			job->progress->setValue(static_cast<int>((job->done + *received) / 1024));
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, part, received, job, c, target]() {
		reply->deleteLater();
		if (*received <= c.size)
			part->write(reply->readAll());
		part->close();
		const QString name = QString::fromLatin1(c.file);
		if (job->progress && job->progress->wasCanceled()) {
			part->remove();
			finish(job, QString(), true);
			return;
		}
		if (*received > c.size) {
			part->remove();
			finish(job, T("FaceMask.DownloadBadFile").arg(name));
			return;
		}
		if (reply->error() != QNetworkReply::NoError) {
			part->remove();
			finish(job, T("FaceMask.DownloadFailed").arg(name, reply->errorString()));
			return;
		}
		if (!fileMatches(part->fileName(), c.size, QString::fromLatin1(c.sha256))) {
			part->remove();
			finish(job, T("FaceMask.DownloadBadFile").arg(name));
			return;
		}
		QFile::remove(target);
		if (!QFile::rename(part->fileName(), target)) {
			part->remove();
			finish(job, T("FaceMask.DownloadFailed").arg(name, T("FaceMask.CannotSave")));
			return;
		}
		job->done += c.size;
		job->index++;
		next(job);
	});
}

} // namespace

bool check()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_ready.load())
		return true;
	if (!missing().empty())
		return false;
	std::string why;
	const QString runtime = QDir(folder()).filePath(QString::fromStdString(onnxRuntimeFileName()));
	if (!loadOnnxRuntime(runtime.toStdString(), &why)) {
		obs_log(LOG_WARNING, "[face-mask] onnxruntime not available: %s", why.c_str());
		return false;
	}
	g_ready.store(true);
	g_generation.fetch_add(1);
	return true;
}

bool ready()
{
	return g_ready.load();
}

int generation()
{
	return g_generation.load();
}

std::string modelPath(const char *file)
{
	const QString path = QDir(folder()).filePath(QStringLiteral("models/") + QString::fromLatin1(file));
	return QFileInfo::exists(path) ? path.toStdString() : std::string();
}

int downloadMegabytes()
{
	qint64 bytes = 0;
	for (const Component &c : missing())
		bytes += c.size;
	return static_cast<int>((bytes + 1024 * 1024 - 1) / (1024 * 1024));
}

bool downloading()
{
	return g_downloading;
}

void download()
{
	if (g_downloading || check())
		return;
	auto job = std::make_shared<Job>();
	job->files = missing();
	for (const Component &c : job->files)
		job->total += c.size;
	job->net = new QNetworkAccessManager();
	if (!!qEnvironmentVariableIsEmpty("MEKETREVE_SELFTEST_FACEMASK")) {
		auto *progress = new QProgressDialog(T("FaceMask.Downloading"), T("FaceMask.Cancel"), 0,
						     static_cast<int>(job->total / 1024),
						     static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		progress->setWindowTitle(T("FaceMask.Name"));
		progress->setAttribute(Qt::WA_DeleteOnClose);
		progress->setMinimumDuration(0);
		progress->show();
		job->progress = progress;
	}
	g_downloading = true;
	obs_log(LOG_INFO, "[face-mask] downloading %d file(s), %lld bytes", static_cast<int>(job->files.size()),
		static_cast<long long>(job->total));
	next(job);
}

void registerSelfTest()
{
	/* download: fetches the components; properties: logs what the
	 * properties of the first face mask show. */
	const QString test = qEnvironmentVariable("MEKETREVE_SELFTEST_FACEMASK");
	if (test != QLatin1String("download") && test != QLatin1String("properties"))
		return;
	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *) {
			if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
				return;
			QTimer::singleShot(0, []() {
				if (qEnvironmentVariable("MEKETREVE_SELFTEST_FACEMASK") == QLatin1String("download")) {
					download();
					return;
				}
				obs_enum_sources(
					[](void *, obs_source_t *source) {
						obs_source_t *mask = nullptr;
						obs_source_enum_filters(
							source,
							[](obs_source_t *, obs_source_t *filter, void *param) {
								if (strcmp(obs_source_get_id(filter),
									   "meketreve_face_mask") == 0)
									*static_cast<obs_source_t **>(param) = filter;
							},
							&mask);
						if (!mask)
							return true;
						obs_properties_t *props = obs_source_properties(mask);
						for (obs_property_t *p = obs_properties_first(props); p;
						     obs_property_next(&p))
							obs_log(LOG_INFO, "[face-mask] selftest property %s: %s",
								obs_property_name(p), obs_property_description(p));
						obs_properties_destroy(props);
						return false;
					},
					nullptr);
			});
		},
		nullptr);
}

} // namespace FaceMask::Setup
