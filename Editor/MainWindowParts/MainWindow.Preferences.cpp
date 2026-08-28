/*
	This file is part of EqualizerAPO-XT, a fork of Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 EqualizerAPO-XT contributors
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <sstream>
#include "services/registry/RegistryPaths.h"
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
#include <QSizePolicy>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QKeySequence>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/EditorSettings.h"
#include "version.h"
#include "Editor/import/LegacyMigration.h"
#include "Editor/widgets/TitleBar.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "skins/SkinThemeData.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


namespace
{
// Version tag for QMainWindow::saveState()/restoreState(). restoreState() applies
// a saved dock/toolbar/menu layout onto the live QMainWindowLayout by matching
// object names; feeding it a state whose structure no longer matches the running
// build can dereference a missing layout item and crash inside QMainWindowLayout
// during the first show(). Passing a version makes restoreState() reject any
// state that was not written with the same number (it returns false and applies
// nothing, so the window just falls back to the default layout once). BUMP THIS
// whenever the QMainWindow's toolbars/dock widgets/menu-widget structure changes,
// so stale layouts from older builds are discarded instead of force-applied.
const int kWindowStateVersion = 1;
}

void MainWindow::loadPreferences()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	const EditorSettings::SkinChoice choice = EditorSettings::readSkinChoice(settings, GUIHelper::isDarkMode());
	skinId = choice.id;
	skinDark = choice.dark;
	currentRenderMode = settings.value(QLatin1String(EditorSettings::Keys::LegacyRows), false).toBool() ? FilterTable::LegacyRows : FilterTable::ModernCards;
	// The layout probe starts from a clean, deterministic right dock without
	// reading or mutating the user's saved window arrangement.
	graphDockPosition = analysisLayoutTestMode ? 2
		: qBound(0, settings.value("interface/graphDockPosition", 1).toInt(), 2);
	applyRedesignPreferences();

	QVariant geometryValue = analysisLayoutTestMode ? QVariant() : settings.value("geometry");
	if (geometryValue.isValid())
		restoreGeometry(geometryValue.toByteArray());
	instantModeCheckBox->setChecked(settings.value("instantMode", true).toBool());
	QString selectedDevice = settings.value("selectedDevice").toString();
	if (!selectedDevice.isEmpty())
	{
		for (int i = 0; i < deviceComboBox->count(); i++)
		{
			shared_ptr<AbstractAPOInfo> apoInfo = deviceComboBox->itemData(i).value<shared_ptr<AbstractAPOInfo>>();
			if (apoInfo != nullptr)
			{
				if (QString::fromStdWString(apoInfo->getDeviceString()).compare(selectedDevice, Qt::CaseInsensitive) == 0)
				{
					deviceComboBox->setCurrentIndex(i);
					break;
				}
			}
		}
	}
	deviceSelected(deviceComboBox->currentIndex());

	int selectedChannelMask = settings.value("selectedChannelMask").toInt();
	if (selectedChannelMask != 0)
	{
		int index = channelConfigurationComboBox->findData(selectedChannelMask);
		if (index != -1)
			channelConfigurationComboBox->setCurrentIndex(index);
	}
	channelConfigurationSelected(channelConfigurationComboBox->currentIndex());

	ui->startFromComboBox->setCurrentIndex(settings.value("analysis/startFrom").toInt());
	ui->analysisChannelComboBox->setCurrentText(settings.value("analysis/channel").toString());
	ui->resolutionSpinBox->setValue(settings.value("analysis/resolution", 65536).toInt());

	QVariant openFilesValue = analysisLayoutTestMode ? QVariant() : settings.value("openFiles");
	int tabIndex = settings.value("tabIndex").toInt();
	if (openFilesValue.isValid())
	{
		QStringList fileList = openFilesValue.toStringList();
		for (int i = 0; i < fileList.size(); i++)
		{
			// Saved absolute paths may predate a config migration; a tab
			// restored into the legacy folder would edit files the audio
			// pipeline no longer reads.
			load(EqAPO::Import::LegacyMigration::adoptMigratedFile(fileList[i]));
			if (i == tabIndex)
				tabIndex = ui->tabWidget->currentIndex();
		}
	}
	ui->tabWidget->setCurrentIndex(tabIndex);
	recentFiles = settings.value("recentFiles").toStringList();
	for (QString& recentFile : recentFiles)
		recentFile = EqAPO::Import::LegacyMigration::adoptMigratedFile(recentFile);
	recentFiles.removeDuplicates();
	updateRecentFiles();

	QVariant languageValue = settings.value("language");
	QLocale::Language language;
	if (languageValue.isValid())
		language = QLocale(languageValue.toString()).language();
	else
		language = QLocale::AnyLanguage;

	for (QAction* action : ui->menuLanguage->actions())
		action->setChecked(action->data().toInt() == language);

	// load window state after initializing channels as it may trigger on_analysisDockWidget_visibilityChanged when analysis panel is detached
	QVariant stateValue = analysisLayoutTestMode ? QVariant() : settings.value("windowState");
	if (stateValue.isValid())
		restoreState(stateValue.toByteArray(), kWindowStateVersion);
	// The saved window state can carry a hidden main toolbar (e.g. the app
	// was closed while the graph was fullscreen, which hides it), and nothing
	// else ever re-shows it - the "the save/tools row is gone" report, with
	// no obvious way back for the user. Toolbar visibility is session
	// chrome, not a persisted preference: every session starts with it on.
	ui->mainToolBar->setVisible(true);
	applyRedesignPreferences();
	updateUndoRedoActions();
}

void MainWindow::savePreferences()
{
	if (noSavePreferences)
		return;

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue("geometry", saveGeometry());
	settings.setValue("windowState", saveState(kWindowStateVersion));
	settings.setValue("instantMode", instantModeCheckBox->isChecked());
	shared_ptr<AbstractAPOInfo> selectedDevice = deviceComboBox->currentData().value<shared_ptr<AbstractAPOInfo>>();
	settings.setValue("selectedDevice", selectedDevice != nullptr ? QString::fromStdWString(selectedDevice->getDeviceString()) : "");
	int channelMask = channelConfigurationComboBox->currentData().toInt();
	settings.setValue("selectedChannelMask", channelMask);

	settings.setValue("analysis/startFrom", ui->startFromComboBox->currentIndex());
	settings.setValue("analysis/channel", ui->analysisChannelComboBox->currentText());
	settings.setValue("analysis/resolution", ui->resolutionSpinBox->value());
	// analysis/zoomX/zoomY are intentionally not persisted: the active graph
	// (EqGraphView) does not expose zoom, and persisting the hidden legacy
	// view's zoom would only resurrect the bug where reloads applied stale
	// zoom to the legacy scene.

	QStringList fileList;
	forEachFilterTable([&](int, FilterTable* filterTable) {
		if (filterTable->getConfigPath().length() > 0)
			fileList.append(filterTable->getConfigPath());
	});
	settings.setValue("openFiles", fileList);
	settings.setValue("tabIndex", ui->tabWidget->currentIndex());
	settings.setValue("recentFiles", recentFiles);
	EditorSettings::writeSkinChoice(settings, { skinId, skinDark });
	settings.setValue(QLatin1String(EditorSettings::Keys::LegacyRows), currentRenderMode == FilterTable::LegacyRows);
	settings.setValue("interface/graphDockPosition", graphDockPosition);

	settings.sync();
}

void MainWindow::updateRecentFiles()
{
	QList<QAction*> actions = ui->menuFile->actions();
	int separatorsFound = 0;
	for (int i = actions.size() - 1; i >= 0; i--)
	{
		QAction* action = actions[i];
		if (action->isSeparator())
		{
			separatorsFound++;

			if (separatorsFound == 1)
			{
				QList<QAction*> newActions;
				for (const QString& recentFile : recentFiles)
				{
					QAction* newAction = new QAction(recentFile, ui->menuFile);
					connect(newAction, SIGNAL(triggered(bool)), this, SLOT(recentFileSelected()));
					newActions.append(newAction);
				}
				ui->menuFile->insertActions(action, newActions);
			}
			else
			{
				break;
			}
		}
		else if (separatorsFound >= 1)
		{
			ui->menuFile->removeAction(action);
		}
	}
}

void MainWindow::setupRedesignActions()
{
	QMenu* interfaceMenu = ui->menuView->addMenu(tr("Interface"));

	interfaceModeActionGroup = new QActionGroup(this);
	interfaceModeActionGroup->setExclusive(true);
	QAction* modernAction = interfaceMenu->addAction(tr("Modern cards"));
	modernAction->setCheckable(true);
	modernAction->setData(static_cast<int>(FilterTable::ModernCards));
	interfaceModeActionGroup->addAction(modernAction);
	QAction* legacyAction = interfaceMenu->addAction(tr("Legacy rows"));
	legacyAction->setCheckable(true);
	legacyAction->setData(static_cast<int>(FilterTable::LegacyRows));
	interfaceModeActionGroup->addAction(legacyAction);
	connect(interfaceModeActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(interfaceModeSelected(QAction*)));

	interfaceMenu->addSeparator();

	skinActionGroup = new QActionGroup(this);
	skinActionGroup->setExclusive(true);
	// Which skins exist and in what order comes from SkinThemeData::roster(); this
	// only supplies the translated names. A roster id with no name here shows as
	// its raw id - visible and reportable, unlike the old hand-written list, where
	// a forgotten skin was simply absent from the menu.
	const QHash<QString, QString> skinNames = {
		{ QStringLiteral("studio"), tr("Studio Glass") },
		{ QStringLiteral("minimal"), tr("Precision Minimal") },
		{ QStringLiteral("soft"), tr("Soft Lab") },
		{ QStringLiteral("rack"), tr("Hardware Rack") },
		{ QStringLiteral("matrix"), tr("Signal Matrix") }
	};
	for (const QString& skinId : SkinThemeData::ids())
	{
		QAction* action = interfaceMenu->addAction(skinNames.value(skinId, skinId));
		action->setCheckable(true);
		action->setData(skinId);
		action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+%1").arg(skinActionGroup->actions().size() + 1)));
		skinActionGroup->addAction(action);
	}
	connect(skinActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(skinSelected(QAction*)));

	darkThemeAction = interfaceMenu->addAction(tr("Dark theme"));
	darkThemeAction->setCheckable(true);
	darkThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
	connect(darkThemeAction, SIGNAL(toggled(bool)), this, SLOT(darkThemeToggled(bool)));

	if (SkinManager::instance()->isHeritage())
	{
		// Heritage is unskinned by definition; the choices come back with the
		// modern presentation.
		skinActionGroup->setEnabled(false);
		darkThemeAction->setEnabled(false);
	}

	interfaceMenu->addSeparator();

	// The gain knobs (Preamp card, biquad gain dial) span a configurable ±range;
	// typed values keep the full command range. Data: the range in dB, 0.0 marks
	// the "Custom..." entry which asks for a number.
	QMenu* knobRangeMenu = interfaceMenu->addMenu(tr("Knob gain range"));
	knobRangeActionGroup = new QActionGroup(this);
	knobRangeActionGroup->setExclusive(true);
	for (double range : { 6.0, 12.0, 20.0, 40.0, 100.0 })
	{
		QAction* action = knobRangeMenu->addAction(tr("±%1 dB").arg(range));
		action->setCheckable(true);
		action->setData(range);
		knobRangeActionGroup->addAction(action);
	}
	QAction* customRangeAction = knobRangeMenu->addAction(tr("Custom..."));
	customRangeAction->setCheckable(true);
	customRangeAction->setData(0.0);
	knobRangeActionGroup->addAction(customRangeAction);
	connect(knobRangeActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(knobRangeSelected(QAction*)));

	// The graph position is chosen explicitly via the dropdown in the analysis
	// panel's control bar (graphPositionComboBox), so no position action here.
	// Checkable so the menu shows whether the mode is on; the slot keeps the
	// check in step when the shortcut toggles it.
	fullscreenGraphAction = interfaceMenu->addAction(tr("Fullscreen graph"));
	fullscreenGraphAction->setCheckable(true);
	fullscreenGraphAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
	connect(fullscreenGraphAction, SIGNAL(triggered()), this, SLOT(toggleGraphFullscreen()));

	// Escape hatch for the custom window chrome: machines where the
	// frameless caption misbehaves can restore the stock Windows title bar
	// (takes effect after a restart, mirroring the language flow).
	interfaceMenu->addSeparator();
	QAction* nativeTitleAction = interfaceMenu->addAction(tr("Native title bar"));
	nativeTitleAction->setCheckable(true);
	nativeTitleAction->setChecked(!useCustomFrame);
	connect(nativeTitleAction, SIGNAL(toggled(bool)), this, SLOT(nativeTitleBarToggled(bool)));
}

void MainWindow::syncKnobRangeActions()
{
	if (knobRangeActionGroup == nullptr)
		return;

	const double knobRange = GUIHelper::knobGainRange();
	QAction* customAction = nullptr;
	bool presetMatched = false;
	for (QAction* action : knobRangeActionGroup->actions())
	{
		const double value = action->data().toDouble();
		if (value == 0.0)
		{
			customAction = action;
			continue;
		}
		const bool matches = value == knobRange;
		action->setChecked(matches);
		presetMatched = presetMatched || matches;
	}
	if (customAction != nullptr)
	{
		customAction->setChecked(!presetMatched);
		customAction->setText(presetMatched
			? tr("Custom...")
			: tr("Custom (±%1 dB)...").arg(knobRange));
	}
}

void MainWindow::dressSkinChrome()
{
	// The toolbar is dressed by the active skin (icons + chrome); re-run on
	// every skin/dark switch so the tinted icons follow the new ink. The
	// menu-only actions (Save As + the Edit menu set) get the matching
	// neutral stroke icons here - they are not on the toolbar, so the skin
	// hook never sees them. (The custom title bar re-tints itself; it listens
	// to the same signal.)
	SkinManager::instance()->styleMainToolbar(ui->mainToolBar);
	const QColor menuInk(SkinManager::instance()->tokens().text);
	ui->actionSaveAs->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save-as.svg"), menuInk, 18));
	ui->actionCut->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/cut.svg"), menuInk, 18));
	ui->actionCopy->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/copy.svg"), menuInk, 18));
	ui->actionPaste->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/paste.svg"), menuInk, 18));
	ui->actionDelete->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/trash.svg"), menuInk, 18));
	ui->actionSelectAll->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/select-all.svg"), menuInk, 18));
}

void MainWindow::applyRedesignPreferences()
{
	// Heritage is a whole presentation, not a skin: re-applying a skin here
	// would stomp applyHeritage's empty stylesheet and native palette with
	// the registry's last skin, mixing modern chrome around legacy rows.
	// Keeping skinId untouched also keeps the saved interface/skin from
	// being clobbered by a heritage session.
	if (!SkinManager::instance()->isHeritage())
	{
		SkinManager::instance()->applySkin(skinId, skinDark);
		skinId = SkinManager::instance()->currentSkinId();
		// The toolbar/menu dressing must not depend on applySkin actually
		// running: a same-skin re-apply is skipped (no skinChanged signal),
		// which used to leave the toolbar on the .ui's legacy .ico icons and
		// without the skin's chrome (rack's rail/LED) in every session that
		// never switched skins - the "default icons appear sometimes" report.
		// dressSkinChrome is idempotent and cheap, so run it directly.
		dressSkinChrome();
	}

	if (interfaceModeActionGroup != nullptr)
	{
		for (QAction* action : interfaceModeActionGroup->actions())
			action->setChecked(action->data().toInt() == static_cast<int>(currentRenderMode));
	}
	if (skinActionGroup != nullptr)
	{
		for (QAction* action : skinActionGroup->actions())
			action->setChecked(action->data().toString() == skinId);
	}
	if (darkThemeAction != nullptr)
	{
		darkThemeAction->blockSignals(true);
		darkThemeAction->setChecked(skinDark);
		darkThemeAction->blockSignals(false);
	}
	syncKnobRangeActions();

	ui->graphPositionComboBox->blockSignals(true);
	ui->graphPositionComboBox->setCurrentIndex(graphDockPosition);
	ui->graphPositionComboBox->blockSignals(false);

	ui->analysisDockWidget->setWindowTitle(tr("Graph"));
	bool graphWasShown = !ui->analysisDockWidget->isHidden();
	Qt::DockWidgetArea area = Qt::TopDockWidgetArea;
	if (graphDockPosition == 1)
		area = Qt::BottomDockWidgetArea;
	else if (graphDockPosition == 2)
		area = Qt::RightDockWidgetArea;
	const bool dockAreaChanged = dockWidgetArea(ui->analysisDockWidget) != area;
	// Re-home the analysis dock only when it is not already in the target area.
	// loadPreferences() runs this both before and after QMainWindow::restoreState();
	// removing and re-adding a dock that restoreState has just laid out can free a
	// QLayoutItem that the dock-area layout still references, and the first show()
	// then dereferences the dangling item (use-after-free crash on heavy/stale saved
	// layouts). Skipping the churn when the dock is already placed avoids creating
	// that dangling item without changing where the dock ends up; a genuine position
	// change (the graph-position dropdown) still re-homes because the current area
	// differs from the target.
	if (dockAreaChanged)
	{
		removeDockWidget(ui->analysisDockWidget);
		addDockWidget(area, ui->analysisDockWidget);
	}
	// The analysis controls live in a compact settings cell beside the graph
	// instead of a full-width strip above it. A top/bottom dock lays the cell
	// to the left of the graph; the narrow right dock stacks it above the
	// graph instead.
	const bool dockOnRight = area == Qt::RightDockWidgetArea;
	ui->analysisDockLayout->setDirection(dockOnRight
		? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);

	// Beside a top/bottom graph this is intentionally a compact 280px settings
	// cell. Above a right-side graph that cap made the controls look like a
	// clipped header stranded on the left. Let the same form fill the dock in
	// that orientation, while preserving the compact horizontal layout.
	QSizePolicy controlPolicy = ui->analysisControlBar->sizePolicy();
	controlPolicy.setHorizontalPolicy(dockOnRight ? QSizePolicy::Expanding : QSizePolicy::Maximum);
	ui->analysisControlBar->setSizePolicy(controlPolicy);
	ui->analysisControlBar->setMaximumWidth(dockOnRight
		? QWIDGETSIZE_MAX : GUIHelper::scale(280.0));

	// The horizontal graph asks for 960px, which is a useful bottom-dock hint
	// but a disastrous right-dock width: QMainWindow otherwise grants almost
	// the whole 1024px window to it and leaves only a card sliver. Give a newly
	// right-docked graph a bounded, proportional starting width. Only do this
	// on an actual area change so a restored or manually dragged width remains
	// the user's choice.
	if (dockAreaChanged && dockOnRight)
	{
		const int minimumGraphDockWidth = GUIHelper::scale(320.0);
		const int maximumGraphDockWidth = GUIHelper::scale(480.0);
		const int filterWorkspaceFloor = GUIHelper::scale(520.0);
		int preferredWidth = qBound(minimumGraphDockWidth, width() * 38 / 100,
			maximumGraphDockWidth);
		preferredWidth = qMin(preferredWidth,
			qMax(minimumGraphDockWidth, width() - filterWorkspaceFloor));
		resizeDocks({ ui->analysisDockWidget }, { preferredWidth }, Qt::Horizontal);
	}
	ui->analysisDockWidget->setVisible(graphFullscreen || graphWasShown);

	setCurrentRenderMode(currentRenderMode);
}

void MainWindow::setCurrentRenderMode(FilterTable::RenderMode mode)
{
	currentRenderMode = mode;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		FilterTable* filterTable = filterTableForTab(i);
		if (filterTable != nullptr)
			filterTable->setRenderMode(currentRenderMode);
	}
}

FilterTable* MainWindow::filterTableForTab(int tabIndex) const
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(tabIndex));
	if (scrollArea == nullptr)
		return nullptr;

	return qobject_cast<FilterTable*>(scrollArea->widget());
}

FilterTable* MainWindow::currentFilterTable() const
{
	return filterTableForTab(ui->tabWidget->currentIndex());
}

void MainWindow::forEachFilterTable(const std::function<void(int, FilterTable*)>& visitor) const
{
	for (int i = 0; i < ui->tabWidget->count(); ++i)
	{
		if (FilterTable* filterTable = filterTableForTab(i))
			visitor(i, filterTable);
	}
}
