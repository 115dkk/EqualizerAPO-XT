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
#include <QProcess>
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

