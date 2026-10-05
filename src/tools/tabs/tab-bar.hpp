/*
Meketreve OBS Essentials - Tabs
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

#include "layout-store.hpp"

#include <obs-frontend-api.h>
#include <obs-hotkey.h>

#include <QHash>
#include <QJsonValue>
#include <QSize>
#include <QObject>
#include <QPointer>

#include <string>
#include <vector>

class QAction;
class QMainWindow;
class QTabBar;
class QToolBar;

/* Owns the tab toolbar and swaps QMainWindow dock layouts per tab. The
 * layouts live in the current profile's folder, so each profile keeps its
 * own set. */
class QTimer;

class TabsController : public QObject {
	Q_OBJECT

public:
	explicit TabsController(QMainWindow *main);
	~TabsController() override;

	/* Called from obs_module_load, before OBS restores its dock layout:
	 * turns the preview (central widget) into a dock so tabs can place it. */
	static void movePreviewToDock(QMainWindow *main);

	void onFrontendEvent(enum obs_frontend_event event);
	void handleHotkey(obs_hotkey_id id);

	QJsonValue exportTabs();
	void importTabs(const QJsonValue &value);
	QString describeTabs(const QJsonValue &value) const;

public slots:
	void switchToIndex(int index);
	void switchRelative(int delta);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void loadProfile(bool startup = false);
	void saveProfile();
	void rebuildTabBar();
	void captureCurrent(const QByteArray &state = QByteArray());
	void applyTab(int configIndex);
	void applyDockList(const QString &id, const QList<DockPlacement> &docks);
	void fillCentralSpace();
	void settleStartupLayout();
	/* Proportional docks: sizes of the visible docked docks for a window size. */
	struct DockSizes {
		QSize window;
		QHash<QString, QSize> sizes;
	};
	DockSizes measureDocks() const;
	void takeReference();
	void useTabReference(const TabLayout &tab);
	void scaleDocks();
	void ensurePreviewVisible(TabLayout &tab);
	void onCurrentChanged(int index);
	void showContextMenu(const QPoint &pos);
	void addTab();
	void renameTab(int index);
	void removeTab(int index);
	void resetTab(int index);
	void setEnabled(bool enabled);
	QString displayName(const TabLayout &tab) const;
	int configIndexForTab(int tabIndex) const;

	void runSelfTest(int step);
	void runImportSelfTest();
	void registerHotkeys();
	void unregisterHotkeys();
	void loadGlobal();
	void saveGlobal();

	QMainWindow *m_main;
	QToolBar *m_toolbar = nullptr;
	QTabBar *m_tabBar = nullptr;
	QAction *m_toggleAction = nullptr;
	TabsConfig m_cfg;
	QString m_profilePath;
	bool m_enabled = true;
	bool m_loaded = false;
	bool m_switching = false;
	/* The layout OBS restored at startup, applied again once the window
	 * reaches its final size (see loadProfile). */
	QByteArray m_startupState;
	QTimer *m_settle = nullptr;
	/* The layout the docks scale from when the window changes size (another
	 * monitor), and the window size the docks have now. The reference only
	 * moves when the user resizes a dock, so moving back and forth between
	 * monitors does not drift. */
	DockSizes m_reference;
	QSize m_layoutWindow;
	QTimer *m_referenceTimer = nullptr;
	QTimer *m_scaleTimer = nullptr;
	bool m_proportional = false; /* on once startup has settled */
	bool m_scaling = false;      /* our own resizeDocks is being applied */
	bool m_frozen = false;       /* shutting down: docks are being removed */
	std::vector<obs_hotkey_id> m_hotkeys;
	std::vector<std::string> m_hotkeyNames;
};
