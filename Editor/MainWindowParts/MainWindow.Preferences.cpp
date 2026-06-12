#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
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

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "version.h"
#include "Editor/widgets/TitleBar.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "SkinManager.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::loadPreferences()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	skinId = settings.value("interface/skin", "studio").toString();
	skinDark = settings.value("interface/dark", GUIHelper::isDarkMode()).toBool();
	currentRenderMode = settings.value("interface/legacyRows", false).toBool() ? FilterTable::LegacyRows : FilterTable::ModernCards;
	// Default to the bottom, like the original Equalizer APO's analysis panel.
	graphDockPosition = qBound(0, settings.value("interface/graphDockPosition", 1).toInt(), 2);
	applyRedesignPreferences();

	QVariant geometryValue = settings.value("geometry");
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
	// The legacy QGraphicsView is hidden and its zoom is no longer user-controlled —
	// EqGraphView auto-fits its data. Reset the scene zoom to a sane default so its
	// getNodes() output (still consumed by EqGraphView) is not skewed by an old saved
	// zoom from previous XT versions.
	analysisPlotScene->setZoom(1.0, 1.0);

	QVariant openFilesValue = settings.value("openFiles");
	int tabIndex = settings.value("tabIndex").toInt();
	if (openFilesValue.isValid())
	{
		QStringList fileList = openFilesValue.toStringList();
		for (int i = 0; i < fileList.size(); i++)
		{
			load(fileList[i]);
			if (i == tabIndex)
				tabIndex = ui->tabWidget->currentIndex();
		}
	}
	ui->tabWidget->setCurrentIndex(tabIndex);
	recentFiles = settings.value("recentFiles").toStringList();
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
	QVariant stateValue = settings.value("windowState");
	if (stateValue.isValid())
		restoreState(stateValue.toByteArray());
	applyRedesignPreferences();
}

void MainWindow::savePreferences()
{
	if (noSavePreferences)
		return;

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue("geometry", saveGeometry());
	settings.setValue("windowState", saveState());
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
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
		if (scrollArea == nullptr)
			continue;
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
		if (filterTable->getConfigPath().length() > 0)
		{
			fileList.append(filterTable->getConfigPath());
		}
	}
	settings.setValue("openFiles", fileList);
	settings.setValue("tabIndex", ui->tabWidget->currentIndex());
	settings.setValue("recentFiles", recentFiles);
	settings.setValue("interface/skin", skinId);
	settings.setValue("interface/dark", skinDark);
	settings.setValue("interface/legacyRows", currentRenderMode == FilterTable::LegacyRows);
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
	const QList<QPair<QString, QString>> skins = {
		{ QStringLiteral("studio"), tr("Studio Glass") },
		{ QStringLiteral("minimal"), tr("Precision Minimal") },
		{ QStringLiteral("soft"), tr("Soft Lab") },
		{ QStringLiteral("rack"), tr("Hardware Rack") },
		{ QStringLiteral("matrix"), tr("Signal Matrix") }
	};
	for (const auto& skin : skins)
	{
		QAction* action = interfaceMenu->addAction(skin.second);
		action->setCheckable(true);
		action->setData(skin.first);
		action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+%1").arg(skinActionGroup->actions().size() + 1)));
		skinActionGroup->addAction(action);
	}
	connect(skinActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(skinSelected(QAction*)));

	darkThemeAction = interfaceMenu->addAction(tr("Dark theme"));
	darkThemeAction->setCheckable(true);
	darkThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
	connect(darkThemeAction, SIGNAL(toggled(bool)), this, SLOT(darkThemeToggled(bool)));

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
	// panel's control bar (graphPositionComboBox); the old implicit
	// "cycle graph position" action is gone.
	QAction* fullscreenGraphAction = interfaceMenu->addAction(tr("Fullscreen graph"));
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

void MainWindow::applyRedesignPreferences()
{
	SkinManager::instance()->applySkin(skinId, skinDark);
	skinId = SkinManager::instance()->currentSkinId();

	// The toolbar is dressed by the active skin (icons + chrome); re-run on
	// every skin/dark switch so the tinted icons follow the new ink. The
	// menu-only actions (Save As + the Edit menu set) get the matching
	// neutral stroke icons here - they are not on the toolbar, so the skin
	// hook never sees them. This retires the last of the 2005-era .ico set
	// from the dropdowns.
	SkinManager::instance()->styleMainToolbar(ui->mainToolBar);
	const QColor menuInk(SkinManager::instance()->tokens().text);
	ui->actionSaveAs->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save-as.svg"), menuInk, 18));
	ui->actionCut->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/cut.svg"), menuInk, 18));
	ui->actionCopy->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/copy.svg"), menuInk, 18));
	ui->actionPaste->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/paste.svg"), menuInk, 18));
	ui->actionDelete->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/trash.svg"), menuInk, 18));
	ui->actionSelectAll->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/select-all.svg"), menuInk, 18));

	// The custom title bar follows the same ink.
	if (titleBar != nullptr)
		titleBar->applySkinIcons();

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
	removeDockWidget(ui->analysisDockWidget);
	Qt::DockWidgetArea area = Qt::TopDockWidgetArea;
	if (graphDockPosition == 1)
		area = Qt::BottomDockWidgetArea;
	else if (graphDockPosition == 2)
		area = Qt::RightDockWidgetArea;
	addDockWidget(area, ui->analysisDockWidget);
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

