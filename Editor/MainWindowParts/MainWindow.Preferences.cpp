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
	skinId = settings.value("interface/skin", "glassy").toString();
	skinDark = settings.value("interface/dark", GUIHelper::isDarkMode()).toBool();
	currentRenderMode = settings.value("interface/legacyRows", false).toBool() ? FilterTable::LegacyRows : FilterTable::ModernCards;
	graphDockPosition = settings.value("interface/graphDockPosition", 0).toInt();
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
	double zoomX = GUIHelper::scaleZoom(settings.value("analysis/zoomX", 1.0).toDouble());
	double zoomY = GUIHelper::scaleZoom(settings.value("analysis/zoomY", 1.0).toDouble());
	analysisPlotScene->setZoom(zoomX, zoomY);
	bool ok;
	int scrollX = GUIHelper::scale(settings.value("analysis/scrollX").toDouble(&ok));
	if (!ok)
		scrollX = round(analysisPlotScene->hzToX(20));
	int scrollY = GUIHelper::scale(settings.value("analysis/scrollY").toDouble(&ok));
	if (!ok)
		scrollY = round(analysisPlotScene->dbToY(22));

	ui->graphicsView->setScrollOffsets(scrollX, scrollY);

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
	settings.setValue("analysis/zoomX", GUIHelper::invScaleZoom(analysisPlotScene->getZoomX()));
	settings.setValue("analysis/zoomY", GUIHelper::invScaleZoom(analysisPlotScene->getZoomY()));
	QScrollBar* hScrollBar = ui->graphicsView->horizontalScrollBar();
	double value = GUIHelper::invScale(hScrollBar->value());
	settings.setValue("analysis/scrollX", value);
	QScrollBar* vScrollBar = ui->graphicsView->verticalScrollBar();
	value = GUIHelper::invScale(vScrollBar->value());
	settings.setValue("analysis/scrollY", value);

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
		{ QStringLiteral("minimal"), tr("Minimal") },
		{ QStringLiteral("glassy"), tr("Glassy") },
		{ QStringLiteral("industrial"), tr("Industrial") },
		{ QStringLiteral("soft"), tr("Soft") }
	};
	for (const auto& skin : skins)
	{
		QAction* action = interfaceMenu->addAction(skin.second);
		action->setCheckable(true);
		action->setData(skin.first);
		action->setShortcut(QKeySequence(QString::number(skinActionGroup->actions().size() + 1)));
		skinActionGroup->addAction(action);
	}
	connect(skinActionGroup, SIGNAL(triggered(QAction*)), this, SLOT(skinSelected(QAction*)));

	darkThemeAction = interfaceMenu->addAction(tr("Dark theme"));
	darkThemeAction->setCheckable(true);
	darkThemeAction->setShortcut(QKeySequence(Qt::Key_D));
	connect(darkThemeAction, SIGNAL(toggled(bool)), this, SLOT(darkThemeToggled(bool)));

	interfaceMenu->addSeparator();
	QAction* cycleGraphAction = interfaceMenu->addAction(tr("Cycle graph position"));
	cycleGraphAction->setShortcut(QKeySequence(Qt::Key_G));
	connect(cycleGraphAction, SIGNAL(triggered()), this, SLOT(cycleGraphPosition()));

	QAction* fullscreenGraphAction = interfaceMenu->addAction(tr("Fullscreen graph"));
	fullscreenGraphAction->setShortcut(QKeySequence(Qt::Key_F));
	connect(fullscreenGraphAction, SIGNAL(triggered()), this, SLOT(toggleGraphFullscreen()));
}

void MainWindow::applyRedesignPreferences()
{
	SkinManager::instance()->applySkin(skinId, skinDark);

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

