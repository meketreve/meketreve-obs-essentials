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

#include <QJsonValue>
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

private:
	void loadProfile();
	void saveProfile();
	void rebuildTabBar();
	void captureCurrent();
	void applyTab(int configIndex);
	void applyDockList(const QString &id, const QList<DockPlacement> &docks);
	void fillCentralSpace();
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
	std::vector<obs_hotkey_id> m_hotkeys;
	std::vector<std::string> m_hotkeyNames;
};
