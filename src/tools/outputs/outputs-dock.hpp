/*
Meketreve OBS Essentials - Outputs
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

#include "output-store.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QHash>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

/* Extra stream destinations. Each output either reuses the main stream's
 * encoders (free, but only while the main stream is live) or encodes on
 * its own. Based on the idea of Aitum Multistream (GPL-2.0). */
class OutputsDock : public QWidget {
	Q_OBJECT

public:
	explicit OutputsDock(QWidget *parent = nullptr);
	~OutputsDock() override;

	void onFrontendEvent(enum obs_frontend_event event);

	QJsonArray exportOutputs() const;
	void importOutputs(const QJsonArray &arr);
	static QString describeOutputs(const QJsonArray &arr);

	/* For the smoke test: start/stop by id and read the state. */
	bool startOutput(const QString &id, bool interactive);
	void stopOutput(const QString &id);
	QString stateText(const QString &id) const;
	QStringList outputIds() const;

private:
	enum class State { Stopped, Starting, Live, Stalled, Reconnecting, Stopping, Error };

	struct Running {
		obs_output_t *output = nullptr;
		obs_service_t *service = nullptr;
		obs_encoder_t *ownVideo = nullptr;
		obs_encoder_t *ownAudio = nullptr;
		State state = State::Stopped;
		qint64 liveSince = 0;
		qint64 lastProgress = 0;
		int lastFrames = -1;
		QString error;
	};

	struct Row {
		QLabel *dot = nullptr;
		QLabel *time = nullptr;
		QPushButton *toggle = nullptr;
	};

	void runSelfTest();
	void loadProfile();
	void saveProfile();
	void rebuildRows();
	void refreshRow(const QString &id);
	void tick();
	void editOutput(const QString &id);
	void removeOutput(const QString &id);
	void startAll(bool followersOnly);
	void stopAll(bool followersOnly);
	void releaseRunning(Running &r);
	void onOutputSignal(obs_output_t *output, int kind, int code);
	qsizetype indexOf(const QString &id) const;

	static void outputStarted(void *data, calldata_t *cd);
	static void outputStopped(void *data, calldata_t *cd);
	static void outputReconnect(void *data, calldata_t *cd);
	static void outputReconnected(void *data, calldata_t *cd);

	QList<OutputConfig> m_outputs;
	QHash<QString, Running> m_running;
	QHash<QString, Row> m_rows;
	QVBoxLayout *m_list = nullptr;
	QLabel *m_empty = nullptr;
	QTimer m_timer;
	QString m_profilePath;
};
