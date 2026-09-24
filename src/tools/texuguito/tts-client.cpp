/*
Meketreve OBS Essentials - Texuguito bot
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
#include "tts-client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>

#include <memory>

namespace GoogleTts {

namespace {

constexpr const char *kRpcId = "jQ1olc";
constexpr const char *kUrl = "https://translate.google.com.br/_/TranslateWebserverUi/data/batchexecute";

struct Job {
	QNetworkAccessManager *net = nullptr;
	QStringList chunks;
	QByteArray mp3;
	std::function<void(QByteArray, QString)> done;
	QPointer<QObject> context;
};

void next(const std::shared_ptr<Job> &job)
{
	if (!job->context)
		return;
	if (job->chunks.isEmpty()) {
		job->done(job->mp3, QString());
		return;
	}
	QNetworkRequest req{QUrl(QString::fromLatin1(kUrl))};
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		      QStringLiteral("application/x-www-form-urlencoded;charset=utf-8"));
	req.setRawHeader("Referer", "http://translate.google.com/");
	req.setRawHeader("User-Agent",
			 "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
			 "Chrome/140.0.0.0 Safari/537.36");
	req.setTransferTimeout(15000);
	QNetworkReply *reply = job->net->post(req, requestBody(job->chunks.takeFirst()));
	QObject::connect(reply, &QNetworkReply::finished, job->context.data(), [job, reply]() {
		reply->deleteLater();
		const QByteArray piece = parseResponse(reply->readAll());
		if (reply->error() != QNetworkReply::NoError || piece.isEmpty()) {
			job->done(QByteArray(), reply->errorString());
			return;
		}
		job->mp3 += piece;
		next(job);
	});
}

} // namespace

QStringList splitText(const QString &text, int maxChars)
{
	QStringList chunks;
	QString current;
	for (const QString &word : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
		/* A single word longer than a chunk is cut where it has to be. */
		QString w = word;
		while (w.size() > maxChars) {
			if (!current.isEmpty()) {
				chunks.append(current);
				current.clear();
			}
			chunks.append(w.left(maxChars));
			w = w.mid(maxChars);
		}
		if (current.isEmpty())
			current = w;
		else if (current.size() + 1 + w.size() <= maxChars)
			current += QLatin1Char(' ') + w;
		else {
			chunks.append(current);
			current = w;
		}
	}
	if (!current.isEmpty())
		chunks.append(current);
	return chunks;
}

QByteArray requestBody(const QString &chunk, const QString &lang)
{
	const QByteArray parameter = QJsonDocument(QJsonArray{chunk, lang, QJsonValue(), QStringLiteral("null")})
					     .toJson(QJsonDocument::Compact);
	const QJsonArray rpc{QJsonArray{QJsonArray{QString::fromLatin1(kRpcId), QString::fromUtf8(parameter),
						   QJsonValue(), QStringLiteral("generic")}}};
	return "f.req=" +
	       QUrl::toPercentEncoding(QString::fromUtf8(QJsonDocument(rpc).toJson(QJsonDocument::Compact))) + "&";
}

QByteArray parseResponse(const QByteArray &body)
{
	static const QRegularExpression audio(QStringLiteral("jQ1olc\",\"\\[\\\\\"(.*)\\\\\"]"));
	for (const QByteArray &line : body.split('\n')) {
		if (!line.contains(kRpcId))
			continue;
		const auto m = audio.match(QString::fromUtf8(line));
		if (m.hasMatch())
			return QByteArray::fromBase64(m.captured(1).toLatin1());
	}
	return QByteArray();
}

void synthesize(QNetworkAccessManager *net, const QString &text, std::function<void(QByteArray, QString)> done,
		QObject *context)
{
	auto job = std::make_shared<Job>();
	job->net = net;
	job->chunks = splitText(text);
	job->done = std::move(done);
	job->context = context;
	if (job->chunks.isEmpty()) {
		job->done(QByteArray(), QStringLiteral("empty text"));
		return;
	}
	next(job);
}

} // namespace GoogleTts
