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
#include "Editor/helpers/BackgroundProbe.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/DeviceFormatBadgeState.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "Editor/MainWindowParts/OpenDocument.h"
#include "ui_MainWindow.h"
#include "devices/DeviceAPOInfo.h"

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
			channelConfigurationComboBox->addItem(tr("From device (%1)").arg(selectedInfo->name), 0);
		else if (apoInfo->getChannelCount() != 0)
			channelConfigurationComboBox->addItem(tr("From device (%1 channels)").arg(apoInfo->getChannelCount()), 0);
		else
			channelConfigurationComboBox->addItem(tr("From device (? channels)"), 0);
	}
	else
	{
		channelConfigurationComboBox->addItem(tr("From device (?)"), 0);
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
		const ChannelLayout::AnalysisLayout layout = ChannelLayout::analysisLayout(
			selectedDevice->getChannelCount(), selectedDevice->getChannelMask(), channelMask);
		vector<wstring> channelNames = ChannelLayout::getChannelNames(layout.channelCount, layout.channelMask);
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
	filterTable->setFocusPolicy(Qt::StrongFocus);

	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);
	filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	filterTable->initialize(scrollArea, outputDevices, inputDevices);
	filterTable->setRenderMode(currentRenderMode);
	const qint64 setupNs = phaseTimer.nsecsElapsed();
	phaseTimer.start();

	// The document is registered before its tab exists: inserting the tab
	// fires currentChanged, whose dirty badge reads it.
	documents.push_back(std::make_unique<OpenDocument>(scrollArea, filterTable, DocumentState(title, tooltip)));
	OpenDocument* document = documents.back().get();
	connect(&document->saveTimer, &QTimer::timeout, this, [this, document]() {
		// Re-resolve the path on fire: the table's config path can change
		// while the debounce is pending.
		const QString currentPath = document->table->getConfigPath();
		if (currentPath.length() > 0)
		{
			// Instant mode shows no '*'; a save that failed puts it back so
			// the tab asks before it is closed.
			if (!save(document->table, currentPath))
			{
				document->state.setDirty(true);
				showDocumentTitle(*document);
			}
			updateDirtyStatus();
		}
	});

	// Insert the still-empty scroll area into the tab widget BEFORE building
	// the rows. Adding it afterwards reparents the finished table under the
	// tab stack, and the app stylesheet then re-resolves against every card
	// widget a second time - over a second on a 300-row config. The insert
	// fires currentChanged for the first tab; its handlers (dirty badge,
	// debounced analysis kick) are the same ones every tab switch runs.
	int tabIndex = ui->tabWidget->addTab(scrollArea, document->state.tabText());
	ui->tabWidget->setTabToolTip(tabIndex, document->state.path());
	const qint64 insertNs = phaseTimer.nsecsElapsed();
	phaseTimer.start();

	filterTable->setLines(configPath, lines);
	TraceF(L"addTab: setup %d, insert %d, setLines %d ms",
		int(setupNs / 1000000), int(insertNs / 1000000), int(phaseTimer.nsecsElapsed() / 1000000));

	return filterTable;
}

void MainWindow::updateDeviceFormatBadge(const shared_ptr<AbstractAPOInfo>& apoInfo)
{
	if (deviceFormatBadge == nullptr)
		return;

	// A later selection supersedes a probe still running for an earlier one.
	const unsigned generation = ++deviceFormatGeneration;
	DeviceFormatFacts facts;
	facts.deviceSelected = apoInfo != nullptr;
	if (apoInfo != nullptr && apoInfo->getTransportLabel() == L"ASIO")
	{
		// An ASIO target is not an endpoint: the format probe cannot reach it.
		// What is known is what the engine host published after the last stream.
		facts.asio = true;
		facts.asioSampleRate = apoInfo->getSampleRate();
		facts.asioChannelCount = apoInfo->getChannelCount();
		facts.asioDeviceName = QString::fromStdWString(apoInfo->getDeviceName());
	}
	else if (apoInfo != nullptr)
	{
		// The endpoint probe goes through COM and the audio service, which can
		// stall on a slow or unplugged device, so it runs off the UI thread
		// (audit #422 TD-04). Until the result arrives the badge keeps what it
		// shows for this same endpoint, and shows nothing for another one.
		const QString endpoint = QString::fromStdWString(apoInfo->getDeviceGuid());
		if (const std::optional<DeviceFormatBadgeState> pending =
			DeviceFormatBadgeState::whileProbing(deviceFormatEndpoint, endpoint))
		{
			applyDeviceFormatBadge(*pending);
			deviceFormatEndpoint.clear();
		}
		BackgroundProbe::runFor(this,
			[deviceGuid = apoInfo->getDeviceGuid()]() { return AudioFormatProbe::probe(deviceGuid); },
			[this, generation, facts, endpoint](const AudioFormatProbe::Result& result) mutable {
				if (generation != deviceFormatGeneration)
					return;
				facts.probe = result;
				applyDeviceFormatBadge(DeviceFormatBadgeState::describe(facts));
				deviceFormatEndpoint = endpoint;
			});
		return;
	}
	applyDeviceFormatBadge(DeviceFormatBadgeState::describe(facts));
	deviceFormatEndpoint.clear();
}

void MainWindow::applyDeviceFormatBadge(const DeviceFormatBadgeState& badge)
{
	if (!badge.visible)
	{
		deviceFormatBadge->setVisible(false);
		deviceFormatBadge->setText(QString());
		return;
	}

	deviceFormatBadge->setText(badge.text);
	deviceFormatBadge->setToolTip(badge.toolTip);
	deviceFormatBadge->setProperty("severity", badge.severity);
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

