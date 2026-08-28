/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QStyle>
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

#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "services/audio/AudioFormatProbe.h"
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

	updateDeviceFormatBadge(apoInfo);

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

	forEachFilterTable([&](int, FilterTable* filterTable) {
		filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	});

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

		vector<wstring> channelNames = ChannelLayout::getChannelNames(channelCount, channelMask);
		for (const wstring& channelName : channelNames)
		{
			ui->analysisChannelComboBox->addItem(QString::fromStdWString(channelName));
		}
	}

	startAnalysis();
}


FilterTable* MainWindow::addTab(QString title, QString tooltip, QString configPath, QList<QString> lines)
{
	QElapsedTimer phaseTimer;
	phaseTimer.start();
	QScrollArea* scrollArea = new QScrollArea(ui->tabWidget);
	scrollArea->setWidgetResizable(true);
	FilterTable* filterTable = new FilterTable();
	connect(filterTable, &FilterTable::configOpenRequested,
		this, [this](const QString& path) { load(path); });
	connect(filterTable, &FilterTable::analysisUpdateRequested,
		this, [this]() { startAnalysis(); });
	scrollArea->setWidget(filterTable);
	filterTable->setAcceptDrops(true);
	filterTable->setFocusPolicy(Qt::WheelFocus);

	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);
	filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	filterTable->initialize(scrollArea, outputDevices, inputDevices);
	filterTable->setRenderMode(currentRenderMode);
	const qint64 setupNs = phaseTimer.nsecsElapsed();
	phaseTimer.start();

	// Insert the still-empty scroll area into the tab widget BEFORE building
	// the rows. Adding it afterwards reparents the finished table under the
	// tab stack, and the app stylesheet then re-resolves against every card
	// widget a second time - over a second on a 300-row config. The insert
	// fires currentChanged for the first tab; its handlers (dirty badge,
	// debounced analysis kick) are the same ones every tab switch runs.
	int tabIndex = ui->tabWidget->addTab(scrollArea, title);
	ui->tabWidget->setTabToolTip(tabIndex, tooltip);
	const qint64 insertNs = phaseTimer.nsecsElapsed();
	phaseTimer.start();

	filterTable->setLines(configPath, lines);
	qDebug("addTab: setup %d, insert %d, setLines %d ms",
		int(setupNs / 1000000), int(insertNs / 1000000), int(phaseTimer.nsecsElapsed() / 1000000));

	return filterTable;
}

void MainWindow::updateDeviceFormatBadge(const shared_ptr<AbstractAPOInfo>& apoInfo)
{
	if (deviceFormatBadge == nullptr)
		return;

	if (apoInfo == nullptr)
	{
		deviceFormatBadge->setVisible(false);
		deviceFormatBadge->setText(QString());
		return;
	}

	QString label;
	QString severity = QStringLiteral("normal");
	QString tooltip;

	// An ASIO target is not an endpoint: the format probe cannot reach it.
	// What is known is what the engine host published after the last stream.
	if (apoInfo->getTransportLabel() == L"ASIO")
	{
		if (apoInfo->getSampleRate() > 0 && apoInfo->getChannelCount() > 0)
		{
			label = tr("ASIO · %0 Hz · %1 ch").arg(apoInfo->getSampleRate()).arg(apoInfo->getChannelCount());
			tooltip = tr("The last ASIO stream on this interface ran at %0 Hz with %1 channels in this direction; "
				"the engine host processes it in a separate process.")
				.arg(apoInfo->getSampleRate()).arg(apoInfo->getChannelCount());
		}
		else
		{
			label = tr("ASIO · no stream yet");
			tooltip = tr("No ASIO application has opened this interface through EqualizerAPO yet. "
				"Pick \"%0 (EQ APO XT)\" as the ASIO driver in the application.")
				.arg(QString::fromStdWString(apoInfo->getDeviceName()));
		}
		deviceFormatBadge->setText(label);
		deviceFormatBadge->setToolTip(tooltip);
		deviceFormatBadge->setProperty("severity", severity);
		deviceFormatBadge->style()->unpolish(deviceFormatBadge);
		deviceFormatBadge->style()->polish(deviceFormatBadge);
		deviceFormatBadge->setVisible(true);
		return;
	}

	AudioFormatProbe::Result r = AudioFormatProbe::probe(apoInfo->getDeviceGuid());
	switch (r.status)
	{
	case AudioFormatProbe::Status::ActiveFloat32:
		label = tr("EQ active · 32-bit float");
		severity = QStringLiteral("normal");
		tooltip = tr("EqualizerAPO is processing this stream natively (IEEE_FLOAT 32-bit, %0 Hz, %1 ch).")
			.arg(r.sampleRate).arg(r.channelCount);
		break;
	case AudioFormatProbe::Status::ActiveFloat64:
		label = tr("EQ active · 64-bit float");
		severity = QStringLiteral("normal");
		tooltip = tr("EqualizerAPO is processing this stream natively (IEEE_FLOAT 64-bit, %0 Hz, %1 ch).")
			.arg(r.sampleRate).arg(r.channelCount);
		break;
	case AudioFormatProbe::Status::Passthrough:
		label = tr("Passthrough · EQ inactive");
		severity = QStringLiteral("warning");
		tooltip = tr("This device's stream format is %0 (%1-bit container). EqualizerAPO only processes IEEE_FLOAT 32/64-bit streams natively, "
			"so audio is forwarded without any filter being applied. Switch the device's default format to a 32-bit IEEE_FLOAT one in "
			"Sound Settings if you need filtering on this device.")
			.arg(QString::fromStdWString(r.subtypeDescription)).arg(r.containerBytes * 8);
		break;
	case AudioFormatProbe::Status::Unknown:
	default:
		deviceFormatBadge->setVisible(false);
		deviceFormatBadge->setText(QString());
		return;
	}

	deviceFormatBadge->setText(label);
	deviceFormatBadge->setToolTip(tooltip);
	deviceFormatBadge->setProperty("severity", severity);
	deviceFormatBadge->style()->unpolish(deviceFormatBadge);
	deviceFormatBadge->style()->polish(deviceFormatBadge);
	deviceFormatBadge->setVisible(true);
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
			*channelMask = ChannelLayout::getDefaultChannelMask((*selectedDevice)->getChannelCount());
	}
}

