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
#include "tab-bar.hpp"
#include "tabs.h"

#include "../config/config-codec.hpp"
#include "../config/config-share.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>

#include <algorithm>
#include <array>
#include <string>

namespace {

constexpr const char *kPreviewDockId = "meketreve-main-canvas";
constexpr const char *kChatDockId = "meketreve-unified-chat";
constexpr const char *kProfileFile = "meketreve-tabs.json";
constexpr const char *kGlobalFile = "tabs.json";
constexpr int kGoToHotkeys = 9;

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString globalConfigPath()
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *file = obs_module_config_path(kGlobalFile);
	const QString path = QString::fromUtf8(file ? file : "");
	bfree(file);
	return path;
}

bool readEnabledFlag()
{
	obs_data_t *data = obs_data_create_from_json_file_safe(globalConfigPath().toUtf8().constData(), "bak");
	if (!data)
		return true;
	obs_data_set_default_bool(data, "enabled", true);
	const bool enabled = obs_data_get_bool(data, "enabled");
	obs_data_release(data);
	return enabled;
}

Qt::DockWidgetArea areaFromName(const QString &area)
{
	if (area == QLatin1String("left"))
		return Qt::LeftDockWidgetArea;
	if (area == QLatin1String("right"))
		return Qt::RightDockWidgetArea;
	if (area == QLatin1String("top"))
		return Qt::TopDockWidgetArea;
	return Qt::BottomDockWidgetArea;
}

QList<QDockWidget *> topLevelDocks(QMainWindow *main)
{
	return main->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
}

void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed);

} // namespace

TabsController::TabsController(QMainWindow *main) : QObject(main), m_main(main)
{
	m_enabled = readEnabledFlag();

	m_tabBar = new QTabBar();
	m_tabBar->setMovable(true);
	m_tabBar->setExpanding(false);
	m_tabBar->setDocumentMode(true);
	m_tabBar->setElideMode(Qt::ElideRight);
	m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

	m_toolbar = new QToolBar(T("Tabs.Toolbar"), main);
	m_toolbar->setObjectName(QStringLiteral("meketreveTabsToolbar"));
	m_toolbar->setMovable(false);
	m_toolbar->setFloatable(false);
	m_toolbar->addWidget(m_tabBar);
	QAction *add = m_toolbar->addAction(QStringLiteral("+"), this, &TabsController::addTab);
	add->setToolTip(T("Tabs.Add"));
	main->addToolBar(Qt::TopToolBarArea, m_toolbar);
	m_toolbar->setVisible(m_enabled);

	connect(m_tabBar, &QTabBar::currentChanged, this, &TabsController::onCurrentChanged);
	connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &TabsController::showContextMenu);
	connect(m_tabBar, &QTabBar::tabMoved, this, [this]() {
		QList<TabLayout> ordered;
		for (int i = 0; i < m_tabBar->count(); i++) {
			const qsizetype ci = m_cfg.indexOf(m_tabBar->tabData(i).toString());
			if (ci >= 0)
				ordered.append(m_cfg.tabs[ci]);
		}
		m_cfg.tabs = ordered;
		saveProfile();
	});

	m_toggleAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tabs.Enable")));
	m_toggleAction->setCheckable(true);
	m_toggleAction->setChecked(m_enabled);
	connect(m_toggleAction, &QAction::toggled, this, &TabsController::setEnabled);

	registerHotkeys();
}

TabsController::~TabsController()
{
	unregisterHotkeys();
}

void TabsController::movePreviewToDock(QMainWindow *main)
{
	if (main->findChild<QDockWidget *>(QString::fromLatin1(kPreviewDockId)))
		return;
	QWidget *cw = main->centralWidget();
	/* Only touch the layout we know: OBS 32.1+ wraps the preview in
	 * "canvasEditor", 32.0 has "preview" directly under "centralwidget". */
	if (!cw || cw->objectName() != QLatin1String("centralwidget") ||
	    (!cw->findChild<QWidget *>(QStringLiteral("canvasEditor")) &&
	     !cw->findChild<QWidget *>(QStringLiteral("preview")))) {
		obs_log(LOG_WARNING, "[tabs] unknown main window layout, the preview stays in the center");
		return;
	}
	if (!obs_frontend_add_dock_by_id(kPreviewDockId, obs_module_text("Tabs.PreviewDock"), cw)) {
		obs_log(LOG_WARNING, "[tabs] could not turn the preview into a dock");
		return;
	}
	auto *hint = new QLabel(T("Tabs.CentralHint"), main);
	hint->setAlignment(Qt::AlignCenter);
	hint->setWordWrap(true);
	hint->setMinimumSize(0, 0);
	hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	main->setCentralWidget(hint);
}

void TabsController::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (m_enabled)
			loadProfile();
		if (qEnvironmentVariableIsSet("MEKETREVE_SELFTEST_DIR"))
			QTimer::singleShot(2000, this, [this]() { runSelfTest(0); });
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		if (m_loaded) {
			captureCurrent();
			saveProfile();
		}
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		if (m_enabled)
			loadProfile();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		if (m_loaded) {
			captureCurrent();
			saveProfile();
		}
		saveGlobal();
		break;
	default:
		break;
	}
}

QString TabsController::displayName(const TabLayout &tab) const
{
	if (tab.id == QLatin1String("live") && tab.name.isEmpty())
		return T("Tabs.Live");
	if (tab.id == QLatin1String("build") && tab.name.isEmpty())
		return T("Tabs.Build");
	return tab.name;
}

int TabsController::configIndexForTab(int tabIndex) const
{
	if (tabIndex < 0 || tabIndex >= m_tabBar->count())
		return -1;
	return static_cast<int>(m_cfg.indexOf(m_tabBar->tabData(tabIndex).toString()));
}

void TabsController::loadProfile()
{
	char *dir = obs_frontend_get_current_profile_path();
	m_profilePath = QString::fromUtf8(dir ? dir : "") + QLatin1Char('/') + QLatin1String(kProfileFile);
	bfree(dir);

	bool ok = false;
	QFile file(m_profilePath);
	if (file.open(QIODevice::ReadOnly)) {
		QString error;
		ok = TabsConfig::fromJson(QJsonDocument::fromJson(file.readAll()).object(), m_cfg, &error);
		if (!ok)
			obs_log(LOG_WARNING, "[tabs] ignoring %s: %s", kProfileFile, error.toUtf8().constData());
	}

	if (!ok) {
		/* First run on this profile: keep the current layout as "My layout". */
		m_cfg = TabsConfig::defaults(QByteArray(), T("Tabs.MyLayout"));
		m_loaded = true;
		rebuildTabBar();
		ensurePreviewVisible(m_cfg.tabs[0]);
		/* The main window is still settling right after loading: size the
		 * preview again once it has, then keep that as "My layout". */
		QTimer::singleShot(500, this, &TabsController::fillCentralSpace);
		QTimer::singleShot(1500, this, [this]() {
			captureCurrent();
			saveProfile();
		});
		obs_log(LOG_INFO, "[tabs] first run on this profile, current layout saved as \"%s\"",
			T("Tabs.MyLayout").toUtf8().constData());
		return;
	}

	m_loaded = true;
	rebuildTabBar();
	const qsizetype ci = m_cfg.indexOf(m_cfg.current);
	if (ci >= 0 && !m_cfg.tabs[ci].state.isEmpty() && m_cfg.tabs[ci].state != m_main->saveState())
		applyTab(static_cast<int>(ci));
	obs_log(LOG_INFO, "[tabs] loaded %d tab(s), current \"%s\"", static_cast<int>(m_cfg.tabs.size()),
		m_cfg.current.toUtf8().constData());
}

void TabsController::saveProfile()
{
	if (!m_loaded || m_profilePath.isEmpty())
		return;
	/* Tab order follows the bar. */
	QSaveFile file(m_profilePath);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "[tabs] could not write %s", m_profilePath.toUtf8().constData());
		return;
	}
	file.write(QJsonDocument(m_cfg.toJson()).toJson(QJsonDocument::Indented));
	if (!file.commit())
		obs_log(LOG_WARNING, "[tabs] could not save %s", m_profilePath.toUtf8().constData());
}

void TabsController::rebuildTabBar()
{
	m_switching = true;
	while (m_tabBar->count() > 0)
		m_tabBar->removeTab(0);
	for (const TabLayout &t : m_cfg.tabs) {
		const int i = m_tabBar->addTab(displayName(t));
		m_tabBar->setTabData(i, t.id);
	}
	const qsizetype ci = m_cfg.indexOf(m_cfg.current);
	m_tabBar->setCurrentIndex(static_cast<int>(ci < 0 ? 0 : ci));
	m_switching = false;
}

void TabsController::captureCurrent()
{
	if (!m_loaded || !m_enabled)
		return;
	const qsizetype ci = m_cfg.indexOf(m_cfg.current);
	if (ci >= 0)
		m_cfg.tabs[ci].state = m_main->saveState();
}

void TabsController::onCurrentChanged(int index)
{
	if (m_switching || !m_loaded)
		return;
	const int ci = configIndexForTab(index);
	if (ci < 0)
		return;
	captureCurrent();
	m_cfg.current = m_cfg.tabs[ci].id;
	applyTab(ci);
	saveProfile();
}

void TabsController::switchToIndex(int index)
{
	if (m_enabled && m_loaded && index >= 0 && index < m_tabBar->count())
		m_tabBar->setCurrentIndex(index);
}

void TabsController::switchRelative(int delta)
{
	const int n = m_tabBar->count();
	if (n > 0)
		switchToIndex(((m_tabBar->currentIndex() + delta) % n + n) % n);
}

void TabsController::applyTab(int configIndex)
{
	TabLayout &tab = m_cfg.tabs[configIndex];
	if (tab.state.isEmpty()) {
		const QList<DockPlacement> docks = tab.docks.isEmpty() ? defaultDocks(tab.id) : tab.docks;
		/* Without either, a tab keeps the layout it was made from. */
		if (!docks.isEmpty())
			applyDockList(tab.id, docks);
		return;
	}
	if (!m_main->restoreState(tab.state))
		obs_log(LOG_WARNING, "[tabs] could not restore the layout of \"%s\"", tab.id.toUtf8().constData());
	m_toolbar->setVisible(m_enabled);
	ensurePreviewVisible(tab);
}

void TabsController::ensurePreviewVisible(TabLayout &tab)
{
	/* The preview dock is new to layouts saved before the plugin, so it
	 * would come back hidden; show it once per tab, then respect the user. */
	if (tab.previewShown)
		return;
	tab.previewShown = true;
	auto *dock = m_main->findChild<QDockWidget *>(QString::fromLatin1(kPreviewDockId));
	if (!dock || dock->isVisible())
		return;
	dock->setFloating(false);
	m_main->addDockWidget(Qt::TopDockWidgetArea, dock);
	dock->show();
	QTimer::singleShot(0, this, &TabsController::fillCentralSpace);
}

void TabsController::applyDockList(const QString &id, const QList<DockPlacement> &docks)
{
	/* Side docks take the full height, so chat and scene lists get room. */
	m_main->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
	m_main->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	m_main->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
	m_main->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	config_t *uc = obs_frontend_get_user_config();
	if (uc)
		config_set_bool(uc, "BasicWindow", "SideDocks", true);

	for (QDockWidget *dock : topLevelDocks(m_main)) {
		const QString name = dock->objectName();
		const bool keep = std::any_of(docks.begin(), docks.end(),
					      [&name](const DockPlacement &d) { return name == d.dock; });
		if (!keep)
			dock->hide();
	}

	QList<QDockWidget *> side, bottom;
	for (const DockPlacement &d : docks) {
		auto *dock = m_main->findChild<QDockWidget *>(d.dock);
		if (!dock) {
			obs_log(LOG_INFO, "[tabs] layout: dock \"%s\" not found", d.dock.toUtf8().constData());
			continue;
		}
		const Qt::DockWidgetArea area = areaFromName(d.area);
		const bool isSide = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
		dock->setFloating(false);
		m_main->addDockWidget(area, dock, isSide ? Qt::Vertical : Qt::Horizontal);
		dock->show();
		if (isSide)
			side.append(dock);
		else if (area == Qt::BottomDockWidgetArea)
			bottom.append(dock);
	}

	const qsizetype ci = m_cfg.indexOf(id);
	if (ci >= 0)
		m_cfg.tabs[ci].previewShown = true;

	QTimer::singleShot(50, this, [this, id, side, bottom]() {
		if (!side.isEmpty())
			m_main->resizeDocks({side.first()},
					    {side.first()->objectName() == QLatin1String(kChatDockId) ? 340 : 300},
					    Qt::Horizontal);
		if (!bottom.isEmpty())
			m_main->resizeDocks({bottom.first()}, {220}, Qt::Vertical);
		fillCentralSpace();
		if (m_cfg.current == id) {
			captureCurrent();
			saveProfile();
		}
	});
}

void TabsController::fillCentralSpace()
{
	auto *dock = m_main->findChild<QDockWidget *>(QString::fromLatin1(kPreviewDockId));
	QWidget *cw = m_main->centralWidget();
	if (!dock || !cw || !dock->isVisible() || dock->isFloating())
		return;
	switch (m_main->dockWidgetArea(dock)) {
	case Qt::TopDockWidgetArea:
	case Qt::BottomDockWidgetArea:
		m_main->resizeDocks({dock}, {dock->height() + cw->height()}, Qt::Vertical);
		break;
	case Qt::LeftDockWidgetArea:
	case Qt::RightDockWidgetArea:
		m_main->resizeDocks({dock}, {dock->width() + cw->width()}, Qt::Horizontal);
		break;
	default:
		break;
	}
}

void TabsController::showContextMenu(const QPoint &pos)
{
	const int index = m_tabBar->tabAt(pos);
	const int ci = configIndexForTab(index);
	QMenu menu;
	if (ci >= 0) {
		const TabLayout &tab = m_cfg.tabs[ci];
		menu.addAction(T("Tabs.Rename"), this, [this, index]() { renameTab(index); });
		QAction *remove = menu.addAction(T("Tabs.Remove"), this, [this, index]() { removeTab(index); });
		remove->setEnabled(tab.isRemovable());
		if (tab.isFixed())
			menu.addAction(T("Tabs.Reset"), this, [this, index]() { resetTab(index); });
		menu.addSeparator();
	}
	menu.addAction(T("Tabs.Add"), this, &TabsController::addTab);
	menu.exec(m_tabBar->mapToGlobal(pos));
}

void TabsController::addTab()
{
	if (!m_loaded)
		return;
	bool ok = false;
	const QString name =
		QInputDialog::getText(m_main, T("Tabs.Add"), T("Tabs.NamePrompt"), QLineEdit::Normal, QString(), &ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return;

	captureCurrent();
	TabLayout tab;
	tab.id = m_cfg.newCustomId();
	tab.name = name;
	tab.state = m_main->saveState();
	tab.previewShown = true;
	m_cfg.tabs.append(tab);
	m_cfg.current = tab.id;
	rebuildTabBar();
	saveProfile();
}

void TabsController::renameTab(int index)
{
	const int ci = configIndexForTab(index);
	if (ci < 0)
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(m_main, T("Tabs.Rename"), T("Tabs.NamePrompt"), QLineEdit::Normal,
						   displayName(m_cfg.tabs[ci]), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	m_cfg.tabs[ci].name = name;
	m_tabBar->setTabText(index, name);
	saveProfile();
}

void TabsController::removeTab(int index)
{
	const int ci = configIndexForTab(index);
	if (ci < 0 || !m_cfg.tabs[ci].isRemovable())
		return;
	const QString msg = T("Tabs.RemoveConfirm").arg(displayName(m_cfg.tabs[ci]));
	if (QMessageBox::question(m_main, T("Tabs.Remove"), msg) != QMessageBox::Yes)
		return;

	const bool wasCurrent = m_cfg.current == m_cfg.tabs[ci].id;
	m_cfg.tabs.removeAt(ci);
	if (wasCurrent) {
		m_cfg.current = m_cfg.tabs.first().id;
		rebuildTabBar();
		applyTab(0);
	} else {
		rebuildTabBar();
	}
	saveProfile();
}

void TabsController::resetTab(int index)
{
	const int ci = configIndexForTab(index);
	if (ci < 0 || !m_cfg.tabs[ci].isFixed())
		return;
	m_cfg.tabs[ci].state.clear();
	m_cfg.tabs[ci].docks.clear();
	m_cfg.tabs[ci].name.clear();
	m_tabBar->setTabText(index, displayName(m_cfg.tabs[ci]));
	if (m_cfg.current == m_cfg.tabs[ci].id)
		applyDockList(m_cfg.tabs[ci].id, defaultDocks(m_cfg.tabs[ci].id));
	else
		m_tabBar->setCurrentIndex(index);
	saveProfile();
}

void TabsController::setEnabled(bool enabled)
{
	if (enabled == m_enabled)
		return;

	if (!enabled) {
		/* Go back to the layout from before the plugin, then hide the bar. */
		if (m_loaded) {
			captureCurrent();
			const qsizetype mine = m_cfg.indexOf(QStringLiteral("mine"));
			if (mine >= 0) {
				m_cfg.current = m_cfg.tabs[mine].id;
				rebuildTabBar();
				applyTab(static_cast<int>(mine));
			}
			saveProfile();
		}
		m_enabled = false;
		m_toolbar->hide();
		saveGlobal();
		QMessageBox::information(m_main, T("Tabs.Toolbar"), T("Tabs.DisabledInfo"));
		return;
	}

	m_enabled = true;
	movePreviewToDock(m_main);
	m_toolbar->show();
	saveGlobal();
	loadProfile();
}

void TabsController::handleHotkey(obs_hotkey_id id)
{
	const auto it = std::find(m_hotkeys.begin(), m_hotkeys.end(), id);
	if (it == m_hotkeys.end())
		return;
	const auto i = static_cast<int>(it - m_hotkeys.begin());
	if (i == 0)
		switchRelative(1);
	else if (i == 1)
		switchRelative(-1);
	else
		switchToIndex(i - 2);
}

/* Developer smoke test, inert unless MEKETREVE_SELFTEST_DIR is set: visits
 * every tab, logs which docks are visible and saves a screenshot of each. */
QJsonValue TabsController::exportTabs()
{
	captureCurrent();
	return m_cfg.toJson();
}

QString TabsController::describeTabs(const QJsonValue &value) const
{
	TabsConfig cfg;
	if (!TabsConfig::fromJson(value.toObject(), cfg, nullptr, false))
		return T("Config.Invalid").arg(QString());
	QStringList names;
	for (const TabLayout &t : cfg.tabs)
		names.append(displayName(t));
	return names.join(QStringLiteral(", "));
}

void TabsController::importTabs(const QJsonValue &value)
{
	TabsConfig in;
	QString error;
	if (!TabsConfig::fromJson(value.toObject(), in, &error, false)) {
		obs_log(LOG_WARNING, "[tabs] import skipped: %s", error.toUtf8().constData());
		return;
	}
	if (!m_enabled || !m_loaded) {
		obs_log(LOG_WARNING, "[tabs] import skipped: tabs are off");
		return;
	}

	captureCurrent();
	for (TabLayout t : in.tabs) {
		if (t.isFixed()) {
			/* Live/Build are replaced in place. */
			TabLayout &mine = m_cfg.tabs[m_cfg.indexOf(t.id)];
			mine.name = t.name;
			mine.state = t.state;
			mine.docks = t.docks;
			mine.previewShown = t.previewShown;
			continue;
		}
		/* Everything else, the sender's "My layout" included, is added as a
		 * new tab so nothing of the user's own is overwritten. */
		QString name = t.name.isEmpty() ? t.id : t.name;
		const auto taken = [this](const QString &n) {
			return std::any_of(m_cfg.tabs.begin(), m_cfg.tabs.end(),
					   [this, &n](const TabLayout &x) { return displayName(x) == n; });
		};
		for (int n = 2; taken(name); n++)
			name = QStringLiteral("%1 (%2)").arg(t.name.isEmpty() ? t.id : t.name).arg(n);
		t.id = m_cfg.newCustomId();
		t.name = name;
		m_cfg.tabs.append(t);
	}

	rebuildTabBar();
	const qsizetype ci = m_cfg.indexOf(m_cfg.current);
	if (ci >= 0)
		applyTab(static_cast<int>(ci));
	saveProfile();
	obs_log(LOG_INFO, "[tabs] imported %d tab(s)", static_cast<int>(in.tabs.size()));
}

void TabsController::runSelfTest(int step)
{
	const QString dir = qEnvironmentVariable("MEKETREVE_SELFTEST_DIR");
	const auto shoot = [this, dir](const QString &name) {
		QStringList visible;
		for (QDockWidget *dock : topLevelDocks(m_main)) {
			if (dock->isVisible())
				visible.append(dock->objectName() + QLatin1Char('@') +
					       QString::number(static_cast<int>(m_main->dockWidgetArea(dock))));
		}
		const QString file = QStringLiteral("%1/%2.png").arg(dir, name);
		const bool ok = m_main->grab().save(file);
		obs_log(LOG_INFO, "[selftest] tab=%s saved=%d visible=%s", name.toUtf8().constData(), ok,
			visible.join(QLatin1Char(' ')).toUtf8().constData());
	};

	if (step < m_tabBar->count()) {
		m_tabBar->setCurrentIndex(step);
		QTimer::singleShot(1500, this, [this, step, shoot]() {
			shoot(QStringLiteral("%1-%2").arg(step).arg(m_tabBar->tabData(step).toString()));
			runSelfTest(step + 1);
		});
		return;
	}
	if (step == m_tabBar->count()) {
		/* A fresh profile must start with its own "My layout". */
		m_tabBar->setCurrentIndex(0);
		char *before = obs_frontend_get_current_profile();
		const std::string original = before ? before : "";
		bfree(before);
		obs_frontend_create_profile("meketreve-selftest");
		QTimer::singleShot(1500, this, [this, original]() {
			char *now = obs_frontend_get_current_profile();
			obs_log(LOG_INFO, "[selftest] profile=%s tabs=%d current=%s file=%s", now, m_tabBar->count(),
				m_cfg.current.toUtf8().constData(), m_profilePath.toUtf8().constData());
			bfree(now);
			obs_frontend_set_current_profile(original.c_str());
			QTimer::singleShot(1500, this, [this]() {
				obs_log(LOG_INFO, "[selftest] back tabs=%d current=%s", m_tabBar->count(),
					m_cfg.current.toUtf8().constData());
				runImportSelfTest();
			});
		});
	}
}

void TabsController::runImportSelfTest()
{
	/* Export -> string -> import must add copies of the custom tabs. */
	const QString text = ConfigCodec::encode(QJsonObject{{QStringLiteral("tabs"), exportTabs()}});
	QJsonObject back;
	QString error;
	const bool decoded = ConfigCodec::decode(text, back, &error);
	const int before = m_tabBar->count();
	importTabs(back.value(QStringLiteral("tabs")));
	obs_log(LOG_INFO, "[selftest] export len=%d decoded=%d tabs %d -> %d", static_cast<int>(text.size()), decoded,
		before, m_tabBar->count());

	char *file = obs_module_file("presets/10-chat-only.json");
	QFile preset(QString::fromUtf8(file ? file : ""));
	bfree(file);
	if (preset.open(QIODevice::ReadOnly))
		importTabs(QJsonDocument::fromJson(preset.readAll()).object().value(QStringLiteral("tabs")));
	const int last = m_tabBar->count() - 1;
	m_tabBar->setCurrentIndex(last);
	QTimer::singleShot(1500, this, [this, last]() {
		QStringList visible;
		for (QDockWidget *dock : topLevelDocks(m_main)) {
			if (dock->isVisible())
				visible.append(dock->objectName());
		}
		obs_log(LOG_INFO, "[selftest] preset tab=%s visible=%s", m_tabBar->tabText(last).toUtf8().constData(),
			visible.join(QLatin1Char(' ')).toUtf8().constData());
		m_main->grab().save(qEnvironmentVariable("MEKETREVE_SELFTEST_DIR") + QStringLiteral("/preset.png"));
		obs_log(LOG_INFO, "[selftest] done");
	});
}

void TabsController::registerHotkeys()
{
	struct Def {
		QByteArray name;
		QString description;
	};
	QList<Def> defs{{"meketreve.tabs.next", T("Tabs.Hotkey.Next")},
			{"meketreve.tabs.previous", T("Tabs.Hotkey.Previous")}};
	for (int n = 1; n <= kGoToHotkeys; n++)
		defs.append({"meketreve.tabs.go." + QByteArray::number(n), T("Tabs.Hotkey.GoTo").arg(n)});

	for (const Def &d : defs) {
		m_hotkeys.push_back(obs_hotkey_register_frontend(d.name.constData(), d.description.toUtf8().constData(),
								 hotkeyCallback, this));
		m_hotkeyNames.push_back(d.name.toStdString());
	}
	loadGlobal();
}

void TabsController::unregisterHotkeys()
{
	for (obs_hotkey_id id : m_hotkeys)
		obs_hotkey_unregister(id);
	m_hotkeys.clear();
	m_hotkeyNames.clear();
}

void TabsController::loadGlobal()
{
	obs_data_t *data = obs_data_create_from_json_file_safe(globalConfigPath().toUtf8().constData(), "bak");
	if (!data)
		return;
	obs_data_t *hotkeys = obs_data_get_obj(data, "hotkeys");
	for (size_t i = 0; hotkeys && i < m_hotkeys.size(); i++) {
		obs_data_array_t *arr = obs_data_get_array(hotkeys, m_hotkeyNames[i].c_str());
		if (arr) {
			obs_hotkey_load(m_hotkeys[i], arr);
			obs_data_array_release(arr);
		}
	}
	obs_data_release(hotkeys);
	obs_data_release(data);
}

void TabsController::saveGlobal()
{
	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", m_enabled);
	obs_data_t *hotkeys = obs_data_create();
	for (size_t i = 0; i < m_hotkeys.size(); i++) {
		obs_data_array_t *arr = obs_hotkey_save(m_hotkeys[i]);
		obs_data_set_array(hotkeys, m_hotkeyNames[i].c_str(), arr);
		obs_data_array_release(arr);
	}
	obs_data_set_obj(data, "hotkeys", hotkeys);
	obs_data_release(hotkeys);
	if (!obs_data_save_json_safe(data, globalConfigPath().toUtf8().constData(), "tmp", "bak"))
		obs_log(LOG_WARNING, "[tabs] could not save %s", kGlobalFile);
	obs_data_release(data);
}

namespace {

QPointer<TabsController> g_tabs;

void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	QPointer<TabsController> self = static_cast<TabsController *>(data);
	QMetaObject::invokeMethod(
		self.data(),
		[self, id]() {
			if (self)
				self->handleHotkey(id);
		},
		Qt::QueuedConnection);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (g_tabs)
		g_tabs->onFrontendEvent(event);
}

} // namespace

void tabs_register(void)
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main)
		return;
	if (readEnabledFlag())
		TabsController::movePreviewToDock(main);
	g_tabs = new TabsController(main);
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	configShareAddSection({QStringLiteral("tabs"), "Config.Section.Tabs",
			       []() { return g_tabs ? g_tabs->exportTabs() : QJsonValue(); },
			       [](const QJsonValue &v) {
				       if (g_tabs)
					       g_tabs->importTabs(v);
			       },
			       [](const QJsonValue &v) {
				       return g_tabs ? g_tabs->describeTabs(v) : QString();
			       }});
}

void tabs_unregister(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	delete g_tabs.data();
}
