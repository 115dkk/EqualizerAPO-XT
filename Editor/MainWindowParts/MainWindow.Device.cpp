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


void MainWindow::deviceSelected(int index)
{
	shared_ptr<AbstractAPOInfo> apoInfo = deviceComboBox->itemData(index).value<shared_ptr<AbstractAPOInfo>>();
	if (apoInfo == nullptr)
		apoInfo = defaultOutputDevice;

	channelConfigurationComboBox->clear();

	const QList<GUIChannelHelper::ChannelConfigurationInfo>& infos = GUIChannelHelper::getInstance()->getChannelConfigurationInfos();

	if (apoInfo != nullptr)
	{
		const GUIChannelHelper::ChannelConfigurationInfo* selectedInfo = nullptr;
		for (const GUIChannelHelper::ChannelConfigurationInfo& info : infos)
		{
			if (info.channelMask == static_cast<int>(apoInfo->getChannelMask()))
			{
				selectedInfo = &info;
				break;
			}
		}

		if (selectedInfo != nullptr)
			channelConfigurationComboBox->addItem(tr("From device") + " (" + selectedInfo->name + ")", 0);
		else if (apoInfo->getChannelCount() != 0)
			channelConfigurationComboBox->addItem((tr("From device") + " (%1 channels)").arg(apoInfo->getChannelCount()), 0);
		else
			channelConfigurationComboBox->addItem(tr("From device") + " (? channels)", 0);
	}
	else
	{
		channelConfigurationComboBox->addItem(tr("From device") + " (?)", 0);
	}

	for (const GUIChannelHelper::ChannelConfigurationInfo& info : infos)
		channelConfigurationComboBox->addItem(info.name, info.channelMask);

	channelConfigurationSelected(channelConfigurationComboBox->currentIndex());
}

void MainWindow::channelConfigurationSelected(int index)
{
	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);

	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
		filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	}

	ui->analysisChannelComboBox->clear();

	if (selectedDevice != nullptr)
	{
		unsigned channelCount = selectedDevice->getChannelCount();
		if (channelMask != 0 && channelMask != static_cast<int>(selectedDevice->getChannelMask()))
		{
			channelCount = 0;
			for (int i = 0; i < 31; i++)
			{
				int channelPos = 1 << i;
				if (channelMask & channelPos)
					channelCount++;
			}
		}
		if (channelCount == 0)
		{
			channelCount = 8;
			channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		}

		vector<wstring> channelNames = ChannelHelper::getChannelNames(channelCount, channelMask);
		for (const wstring& channelName : channelNames)
		{
			ui->analysisChannelComboBox->addItem(QString::fromStdWString(channelName));
		}
	}

	startAnalysis();
}


FilterTable* MainWindow::addTab(QString title, QString tooltip, QString configPath, QList<QString> lines)
{
	QScrollArea* scrollArea = new QScrollArea(ui->tabWidget);
	scrollArea->setWidgetResizable(true);
	FilterTable* filterTable = new FilterTable(this);
	scrollArea->setWidget(filterTable);
	filterTable->setAcceptDrops(true);
	filterTable->setFocusPolicy(Qt::WheelFocus);

	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);
	filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	filterTable->initialize(scrollArea, outputDevices, inputDevices);
	filterTable->setLines(configPath, lines);

	int tabIndex = ui->tabWidget->addTab(scrollArea, title);
	ui->tabWidget->setTabToolTip(tabIndex, tooltip);

	return filterTable;
}

void MainWindow::getDeviceAndChannelMask(shared_ptr<AbstractAPOInfo>* selectedDevice, int* channelMask)
{
	*selectedDevice = deviceComboBox->currentData().value<shared_ptr<AbstractAPOInfo>>();
	if (*selectedDevice == nullptr)
		*selectedDevice = defaultOutputDevice;

	*channelMask = channelConfigurationComboBox->currentData().toInt();
	if (*channelMask == 0 && selectedDevice->get() != nullptr)
	{
		*channelMask = (*selectedDevice)->getChannelMask();

		if (*channelMask == 0)
			*channelMask = ChannelHelper::getDefaultChannelMask((*selectedDevice)->getChannelCount());
	}
}

