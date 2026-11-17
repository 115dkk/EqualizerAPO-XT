/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "services/logging/Logging.h"
#include "platform/windows/ElevatedLaunch.h"
#include "platform/windows/Win32Error.h"
#include "audio/ChannelLayout.h"
#include "services/audio/AudioFormatProbe.h"
#include "services/update/UpdateSession.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/helpers/BackgroundProbe.h"
#include "Editor/helpers/TroubleshootingNotices.h"
#include "platform/windows/WindowsPath.h"
#include "Editor/helpers/GUIChannelHelper.h"
// Complete type for frameChildForwarder's destructor in ~MainWindow.
#include "Editor/helpers/WindowFrameHitTest.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/MainToolbarKit.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "Editor/MainWindowParts/OpenDocument.h"
#include "services/install/ApoRegistration.h"
#include "services/windows/WindowsService.h"
#include "services/windows/AudioServiceNames.h"
#include "services/security/AudioEngineAccess.h"
#include "services/registry/RegistryPaths.h"
#include "SkinManager.h"
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

template<class T> QList<T> MainWindow::toQList(const std::vector<T>& vector)
{
	QList<T> list;
	list.reserve(static_cast<int>(vector.size()));
	for (T t : vector)
		list.append(t);

	return list;
}

MainWindow::MainWindow(QDir configDir, UpdateSession* updateSession, QWidget* parent,
	bool analysisLayoutTestMode)
	: QMainWindow(parent),
	ui(std::make_unique<Ui::MainWindow>()),
	configDir(configDir),
	updateSession(updateSession),
	analysisLayoutTestMode(analysisLayoutTestMode)
{
	outputDevices = toQList(DeviceAPOInfo::loadAllInfos(false));
	inputDevices = toQList(DeviceAPOInfo::loadAllInfos(true));

	defaultOutputDevice = nullptr;
	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
	{
		if (apoInfo->isDefaultDevice())
		{
			defaultOutputDevice = apoInfo;
			break;
		}
	}

	ui->setupUi(this);
	resize(QSize(1024, 768));

	// Audit #250 F019: one display-version rule for all binaries (version.h).
	QString version = QString::fromStdWString(eapoDisplayVersionW());
	setWindowTitle(tr("Equalizer APO %0 Configuration Editor").arg(version));

	// Custom window chrome (title bar + menu bar in the menu-widget slot);
	// must run after the title is set so the TitleBar picks it up, and before
	// preferences apply skin icons to it.
	setupWindowChrome();

	// Every live skin/dark switch re-dresses the tinted chrome icons. The
	// switch slots apply the skin directly (they tear the rows down first),
	// so the redress must ride the signal, not applyRedesignPreferences.
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		dressSkinChrome();
	});

	MainToolbarKit::Content toolbarContent;
	toolbarContent.instantMode = tr("Instant mode");
	toolbarContent.saved = tr("Saved");
	toolbarContent.device = tr("Device");
	toolbarContent.channels = tr("Channels");
	toolbarContent.instantModeToolTip = tr("Changes are saved immediately");
	toolbarContent.savedToolTip = tr("Current file save state");
	toolbarContent.formatToolTip = tr("Whether EqualizerAPO is processing this device's stream natively, or forwarding it without applying filters.");
	const MainToolbarKit::Widgets toolbarWidgets =
		MainToolbarKit::populate(ui->mainToolBar, toolbarContent, false);
	instantModeCheckBox = toolbarWidgets.instantMode;
	dirtyStatusLabel = toolbarWidgets.dirtyStatus;
	deviceComboBox = toolbarWidgets.device;
	deviceFormatBadge = toolbarWidgets.deviceFormat;
	channelConfigurationComboBox = toolbarWidgets.channels;
	connect(instantModeCheckBox, SIGNAL(toggled(bool)), this, SLOT(instantModeEnabled(bool)));
	connect(deviceComboBox, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::deviceSelected);

	QStandardItemModel* model = qobject_cast<QStandardItemModel*>(deviceComboBox->model());
	if (defaultOutputDevice != nullptr)
		deviceComboBox->addItem(tr("Default (%1 - %2)").arg(QString::fromStdWString(defaultOutputDevice->getConnectionName()),
			QString::fromStdWString(defaultOutputDevice->getDeviceName())), QVariant::fromValue(shared_ptr<AbstractAPOInfo>()));

	deviceComboBox->addItem(tr("Playback devices:"));
	QStandardItem* item = model->item(model->rowCount() - 1);
	QFont font = item->font();
	font.setBold(true);
	item->setFont(font);
	item->setSelectable(false);

	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
		if (apoInfo->isInstalled())
			deviceComboBox->addItem(QString::fromStdWString(apoInfo->getConnectionName()) + " - " + QString::fromStdWString(apoInfo->getDeviceName()), QVariant::fromValue(apoInfo));

	deviceComboBox->addItem(tr("Capture devices:"));
	item = model->item(model->rowCount() - 1);
	item->setFont(font);
	item->setSelectable(false);

	for (shared_ptr<AbstractAPOInfo>& apoInfo : inputDevices)
		if (apoInfo->isInstalled())
			deviceComboBox->addItem(QString::fromStdWString(apoInfo->getConnectionName()) + " - " + QString::fromStdWString(apoInfo->getDeviceName()), QVariant::fromValue(apoInfo));

	connect(channelConfigurationComboBox, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::channelConfigurationSelected);

	eqGraphView = new EqGraphView(ui->dockWidgetContents);
	eqGraphView->setObjectName(QStringLiteral("ModernAnalysisGraph"));
	ui->analysisDockLayout->insertWidget(1, eqGraphView, 1);

	ui->analysisControlBar->setObjectName(QStringLiteral("analysisControlBar"));
	ui->analysisControlBar->setAttribute(Qt::WA_StyledBackground, true);
	for (QLabel* label : { ui->startFromLabel, ui->analysisChannelLabel, ui->resolutionLabel })
		label->setObjectName(QStringLiteral("AnalysisFormLabel"));
	// Ignored rather than Preferred: these sit in a bar capped at
	// analysisControlBarWidth, and a combo's own size hint is its widest item
	// plus the drop-down and whatever padding the active skin gives it. That
	// hint has always been larger than the column can offer, so the layout
	// laid them out at their minimum and the bar clipped their right edge -
	// which reads as the graph pane eating
	// into the bar. With the hint ignored they take the column's width instead
	// and elide, which is what a fixed-width bar needs them to do.
	for (QComboBox* combo : { ui->startFromComboBox, ui->analysisChannelComboBox, ui->graphPositionComboBox })
	{
		combo->setObjectName(QStringLiteral("AnalysisFormCombo"));
		combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(6);
	}
	ui->resolutionSpinBox->setObjectName(QStringLiteral("AnalysisFormSpin"));
	ui->resolutionSpinBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	// With the hints ignored the control column has nothing left asking for
	// width, so it has to be told to take what is spare. The label column keeps
	// its own width, which is what aligns the four captions.
	ui->analysisControlLayout->setColumnStretch(1, 1);
	for (QFrame* chip : { ui->peakChip, ui->latencyChip, ui->initChip, ui->cpuChip })
	{
		chip->setObjectName(QStringLiteral("AnalysisStatChip"));
		chip->setAttribute(Qt::WA_StyledBackground, true);
	}
	for (QLabel* label : { ui->peakGainLabel, ui->latencyLabel, ui->initTimeLabel, ui->cpuUsageLabel })
		label->setObjectName(QStringLiteral("AnalysisStatLabel"));
	for (QLabel* value : { ui->peakGainValueLabel, ui->latencyValueLabel, ui->initTimeValueLabel, ui->cpuUsageValueLabel })
	{
		value->setObjectName(QStringLiteral("AnalysisStatValue"));
		value->setProperty("severity", QStringLiteral("normal"));
	}
	ui->tabWidget->setObjectName(QStringLiteral("MainTabWidget"));
	setupAnalysisMetricControls();

	analysisThread = std::make_unique<AnalysisThread>();
	analysisThread->start();
	connect(analysisThread.get(), SIGNAL(analysisFinished()), this, SLOT(updateAnalysisPanel()));

	// Derive the language roster from the catalogs that actually shipped
	// (:/translations/Editor_<code>.qm) instead of a hand-maintained list, so
	// adding a translation cannot miss this menu (and a removed one cannot
	// linger). English is the source language and always present.
	QList<QLocale::Language> languages;
	languages << QLocale::AnyLanguage << QLocale::English;
	const QStringList catalogs = QDir(QStringLiteral(":/translations")).entryList(
		QStringList() << QStringLiteral("Editor_*.qm"), QDir::Files, QDir::Name);
	for (const QString& catalog : catalogs)
	{
		QString code = catalog.mid(7); // after "Editor_"
		code.chop(3); // before ".qm"
		QLocale::Language language = QLocale(code).language();
		if (language != QLocale::English && language != QLocale::AnyLanguage && !languages.contains(language))
			languages.append(language);
	}

	QLocale autoLocale = QLocale::system();
	if (autoLocale.language() != QLocale::English && !languages.contains(autoLocale.language()))
		autoLocale = QLocale("en");
	for (QLocale::Language language : languages)
	{
		const QLocale locale = language == QLocale::AnyLanguage ? autoLocale : QLocale(language);
		// US English names itself "American English"; the menu calls the
		// source language plain English. Other English locales keep their
		// own names.
		QString languageName = locale.nativeLanguageName();
		if (locale.language() == QLocale::English && locale.territory() == QLocale::UnitedStates)
			languageName = QStringLiteral("English");
		QString text;
		if (language == QLocale::AnyLanguage)
			text = tr("Automatic (%0)").arg(languageName);
		else
			text = languageName;
		if(text[0].isLower())
			text[0] = text[0].toUpper();
		QAction* action = ui->menuLanguage->addAction(text);
		action->setData(language);
		action->setCheckable(true);
		connect(action, SIGNAL(triggered(bool)), this, SLOT(languageSelected(bool)));
	}

	setupRedesignActions();
	loadPreferences();
	watchForPendingUpdate();
}

void MainWindow::watchForPendingUpdate()
{
	// The background download (main.cpp) starts 60s after launch and stages
	// silently; this poll announces it without a manual check. 20s is
	// far below the human noticing threshold for "eventually told me" and far
	// above any cost concern for an atomic-bool read.
	if (updateSession == nullptr)
		return;

	updateNoticeTimer = new QTimer(this);
	updateNoticeTimer->setInterval(20000);
	connect(updateNoticeTimer, &QTimer::timeout, this, [this]() {
		if (!updateSession->hasPendingUpdate())
			return;
		updateNoticeTimer->stop();

		if (updateToast == nullptr)
			updateToast = new UpdateToast(centralWidget());
		updateToast->showMessage(downloadedUpdateMessage());
	});
	updateNoticeTimer->start();
}

QString MainWindow::downloadedUpdateMessage() const
{
	const QString version = QString::fromStdWString(updateSession->pendingUpdateVersion());
	return version.isEmpty()
		? tr("An update has been downloaded and will be applied when you close the editor.")
		: tr("Update %0 has been downloaded and will be applied when you close the editor.").arg(version);
}

void MainWindow::on_actionRestartAudioService_triggered()
{
	if (!ui->actionRestartAudioService->isEnabled())
		return;
	if (QMessageBox::question(this, tr("Restart Windows audio"),
		tr("Restart Windows audio now? Sound stops for a few seconds."),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
		restartWindowsAudio();
}

void MainWindow::restartWindowsAudio()
{
	if (!ui->actionRestartAudioService->isEnabled())
		return;
	ui->actionRestartAudioService->setEnabled(false);
	// Work captures no window state. BackgroundProbe drops the completion if
	// this window is destroyed while UAC or the service restart is pending.
	BackgroundProbe::runFor(this, [] {
		return TroubleshootingNotices::restartNotice(audioengine::requestRestart(pathutil::exePath(),
			[](const std::wstring& exe, const std::wstring& args) {
				return winutil::launchElevated(exe, args, true);
			}));
	}, [this](TroubleshootingNotices::RestartNotice notice) {
		ui->actionRestartAudioService->setEnabled(true);
		switch (notice)
		{
		case TroubleshootingNotices::RestartNotice::Restarted:
			QMessageBox::information(this, tr("Restart Windows audio"),
				tr("Windows audio has been restarted."));
			break;
		case TroubleshootingNotices::RestartNotice::Failed:
			QMessageBox::warning(this, tr("Restart Windows audio"),
				tr("Windows audio could not be restarted. Restart the computer to load the installed version."));
			break;
		case TroubleshootingNotices::RestartNotice::None:
			break;
		}
	});
}

void MainWindow::on_actionCheckForUpdates_triggered()
{
	if (!ui->actionCheckForUpdates->isEnabled())
		return;
	if (updateSession == nullptr)
	{
		showManualCheckNotice(manualCheckNotice(false, UpdateDownloadStatus::Idle));
		return;
	}

	const UpdateCheckStart start = updateSession->checkNow();
	const UpdateDownloadStatus status = updateSession->downloadStatus();
	switch (TroubleshootingNotices::updateAction(start, status))
	{
	case TroubleshootingNotices::UpdateAction::None:
		return;
	case TroubleshootingNotices::UpdateAction::ShowNotice:
		showManualCheckNotice(manualCheckNotice(true,
			start == UpdateCheckStart::AlreadyStaged ? UpdateDownloadStatus::Staged : status));
		return;
	case TroubleshootingNotices::UpdateAction::Poll:
		break;
	}

	ui->actionCheckForUpdates->setEnabled(false);
	if (manualUpdateTimer == nullptr)
	{
		manualUpdateTimer = new QTimer(this);
		manualUpdateTimer->setInterval(500);
		connect(manualUpdateTimer, &QTimer::timeout, this, [this]() {
			const UpdateDownloadStatus status = updateSession->downloadStatus();
			if (TroubleshootingNotices::updateAction(UpdateCheckStart::AlreadyRunning, status)
				== TroubleshootingNotices::UpdateAction::Poll)
				return;
			manualUpdateTimer->stop();
			ui->actionCheckForUpdates->setEnabled(true);
			showManualCheckNotice(manualCheckNotice(true, status));
		});
	}
	manualUpdateTimer->start();
}

void MainWindow::showManualCheckNotice(ManualCheckNotice notice)
{
	const QString title = tr("Check for updates");
	switch (notice)
	{
	case ManualCheckNotice::NotInstalledCopy:
		QMessageBox::information(this, title,
			tr("This copy was not installed by the installer, so it cannot update itself."));
		break;
	case ManualCheckNotice::UpToDate:
		QMessageBox::information(this, title, tr("You have the latest version (%1).")
			.arg(QString::fromStdWString(eapoDisplayVersionW())));
		break;
	case ManualCheckNotice::Downloaded:
		QMessageBox::information(this, title, downloadedUpdateMessage());
		break;
	case ManualCheckNotice::Failed:
		QMessageBox::warning(this, title,
			tr("Could not check for updates. Check the internet connection and try again."));
		break;
	case ManualCheckNotice::Checking:
		break;
	}
}

void MainWindow::on_actionReportProblem_triggered()
{
	QDesktopServices::openUrl(QUrl(QStringLiteral(EAPO_REPO_URL "/issues/new")));
}

MainWindow::~MainWindow()
= default;

void MainWindow::doChecks()
{
	// Query only: no transition wait on the UI thread (audit #467 TD-13).
	bool audioServiceRunning = false;
	try
	{
		winutil::UniqueServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
		if (manager)
		{
			WindowsService audio(manager.get(), audioServiceName, SERVICE_QUERY_STATUS);
			audioServiceRunning = audio.getState() == SERVICE_RUNNING;
			if (!audioServiceRunning)
				QMessageBox::warning(this, tr("Windows audio service"),
					tr("The Windows audio service is not running. Sound will not play until it is started."));
		}
	}
	catch (const WindowsServiceError& e)
	{
		LogF(L"Could not query Windows audio service: %s", e.getMessage().c_str());
	}

	if (audioServiceRunning && !engineRestartOffered)
	{
		const auto running = eapo::ipc::beacon::readPublished();
		const auto installed = eapo::ipc::beacon::builtVersion();
		if (TroubleshootingNotices::shouldOfferEngineRestart(audioServiceRunning,
			eapo::ipc::beacon::classify(running, installed)))
		{
			engineRestartOffered = true;
			if (QMessageBox::question(this, tr("Audio engine out of date"),
				tr("The audio engine is still running Equalizer APO %1, but %2 is installed. Windows audio has to restart before the new version takes over.\n\nRestart Windows audio now? Sound stops for a few seconds.")
					.arg(TroubleshootingNotices::formatVersion(*running), TroubleshootingNotices::formatVersion(installed)),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
				restartWindowsAudio();
		}
	}

	// AuthZ can resolve account membership: keep it on the same worker as
	// the slow endpoint probes, not on the UI thread (audit #467 TD-18).
	BackgroundProbe::runFor(this, [] {
		QStringList unreadable;
		for (const wchar_t* value : {installPathValueName, configPathValueName})
		{
			try
			{
				auto& registry = systemRegistry();
				if (!registry.valueExists(APP_REGPATH, value))
					continue;
				const std::wstring folder = registry.readValue(APP_REGPATH, value);
				if (!folder.empty() && !AudioEngineAccess::isReadableByAudioEngine(folder))
					unreadable.append(QString::fromStdWString(folder));
			}
			catch (const RegistryError& e)
			{
				LogFStatic(L"Could not read startup access-check folder: %s", e.getMessage().c_str());
			}
		}
		return unreadable;
	}, [this](const QStringList& unreadable) {
		for (const QString& folder : unreadable)
			QMessageBox::warning(this, tr("Audio folder access"),
				tr("The Windows audio service cannot read this folder:\n%0\n\nRun tools\\Repair-EqualizerAPO.ps1 from the installation folder as administrator to repair access.").arg(folder));
	});

	if (!ApoRegistration::verify(systemRegistry(), false).valid())
	{
		if (QMessageBox::warning(this, tr("Registry problem"), tr("A registry value that is required for the operation of Equalizer APO is not set correctly.\nDo you want to run the Device Selector application to fix the problem?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}

	if (defaultOutputDevice != nullptr && !defaultOutputDevice->isInstalled())
	{
		if (QMessageBox::warning(this, tr("APO not installed to device"), tr("Equalizer APO has not been installed to the selected device.\nDo you want to run the Device Selector application to fix the problem?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}

	// Scan every installed endpoint and warn once if any of them currently
	// uses a stream format we cannot process natively. The APO still passes
	// audio through in that case, but no filters are applied — without this
	// warning the user would just see "EQ has no effect" with no explanation.
	// The probes go through COM and the audio service, which can stall on a
	// slow or unplugged device, so they run off the UI thread (audit #422
	// TD-04); the window is usable meanwhile, and the remaining check follows
	// the warning in the order the two always had.
	struct ProbeTarget
	{
		std::wstring deviceGuid;
		QString name;
		bool input = false;
	};
	std::vector<ProbeTarget> targets;
	auto collect = [&targets](const QList<shared_ptr<AbstractAPOInfo>>& list)
	{
		for (const shared_ptr<AbstractAPOInfo>& info : list)
		{
			if (info == nullptr || !info->isInstalled())
				continue;
			targets.push_back({ info->getDeviceGuid(), QString::fromStdWString(info->getConnectionName())
				+ " - " + QString::fromStdWString(info->getDeviceName()), info->isInput() });
		}
	};
	collect(outputDevices);
	collect(inputDevices);
	BackgroundProbe::runFor(this,
		[targets]() {
			std::vector<AudioFormatProbe::Result> results;
			results.reserve(targets.size());
			for (const ProbeTarget& target : targets)
				results.push_back(AudioFormatProbe::probe(target.deviceGuid, target.input));
			return results;
		},
		[this, targets](const std::vector<AudioFormatProbe::Result>& results) {
			QStringList passthroughDevices;
			for (size_t i = 0; i < targets.size() && i < results.size(); i++)
			{
				const AudioFormatProbe::Result& r = results[i];
				if (AudioFormatProbe::isPassthrough(r.status))
				{
					QString label = targets[i].name
						+ "  (" + QString::fromStdWString(r.subtypeDescription)
						+ ", " + QString::number(r.containerBytes * 8) + "-bit)";
					passthroughDevices.append(label);
				}
			}
			if (!passthroughDevices.isEmpty())
			{
				QMessageBox::warning(this,
					tr("EQ inactive on some devices"),
					tr("EqualizerAPO can only process IEEE_FLOAT 32/64-bit streams natively. The following installed devices currently use a different format, "
					   "so audio passes through them without any filter being applied:\n\n%0\n\nThis is not a crash — sound still reaches the device, but no EQ. "
					   "Switch the device's default format to a 32-bit IEEE_FLOAT one in Sound Settings if you need filtering on them.")
					.arg(passthroughDevices.join('\n')));
			}
			checkAudioEnhancements();
		});
}

void MainWindow::checkAudioEnhancements()
{
	AbstractAPOInfo* disabledApoInfo = nullptr;
	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
	{
		if (apoInfo->isInstalled() && apoInfo->isEnhancementsDisabled())
		{
			disabledApoInfo = apoInfo.get();
			break;
		}
	}

	if (disabledApoInfo == nullptr)
	{
		for (shared_ptr<AbstractAPOInfo>& apoInfo : inputDevices)
		{
			if (apoInfo->isInstalled() && apoInfo->isEnhancementsDisabled())
			{
				disabledApoInfo = apoInfo.get();
				break;
			}
		}
	}

	if (disabledApoInfo != nullptr)
	{
		if (QMessageBox::warning(this, tr("Audio enhancements disabled"), tr("Audio enhancements are not enabled for the device\n%0 %1.\nDo you want to run the Device Selector application to fix the problem?").arg(QString::fromStdWString(disabledApoInfo->getConnectionName())).arg(QString::fromStdWString(disabledApoInfo->getDeviceName())), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}
}

void MainWindow::on_actionApoSettings_triggered()
{
	runDeviceSelector();
}

void MainWindow::on_actionOpenProgramFolder_triggered()
{
	QDesktopServices::openUrl(
		QUrl::fromLocalFile(QCoreApplication::applicationDirPath()));
}

void MainWindow::runDeviceSelector()
{
	// cannot use QProcess::startDetached because of UAC
	wstring file = (QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/DeviceSelector.exe")).toStdWString();
	// One UAC ask, decline respected (audit #250 C1, maintainer decision
	// 2026-08-09). The helper returns GetLastError from ShellExecuteExW, so
	// only ERROR_CANCELLED is reported as a declined prompt; another launch
	// failure carries its Windows message (audit #467 TD-62).
	const auto launch = winutil::launchElevated(file, L"", false, SW_SHOWNORMAL);
	switch (winutil::elevatedLaunchMessage(launch))
	{
	case winutil::ElevatedLaunchMessage::ApprovalDeclined:
		QMessageBox::information(this, tr("Device Selector"),
			tr("The Device Selector was not opened. It needs administrator approval to manage audio devices; you can try again anytime via File > APO settings."));
		break;
	case winutil::ElevatedLaunchMessage::Failed:
		QMessageBox::warning(this, tr("Device Selector"), tr("The Device Selector could not be opened:\n%1")
			.arg(QString::fromStdWString(win32::errorMessage(launch.error))));
		break;
	case winutil::ElevatedLaunchMessage::None:
		break;
	}
}
