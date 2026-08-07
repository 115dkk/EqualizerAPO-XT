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

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "helpers/AudioFormatProbe.h"
#include "helpers/VelopackBootstrap.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/MainToolbarKit.h"
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

template<class T> QList<T> MainWindow::toQList(const std::vector<T>& vector)
{
	QList<T> list;
	list.reserve(static_cast<int>(vector.size()));
	for (T t : vector)
		list.append(t);

	return list;
}

MainWindow::MainWindow(QDir configDir, QWidget* parent)
	: QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()), configDir(configDir)
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
	resize(GUIHelper::scale(QSize(1024, 768)));

	QString version = QString("%0.%1").arg(MAJOR).arg(MINOR);
	if (REVISION != 0)
		version += QString(".%0").arg(REVISION);
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
		deviceComboBox->addItem(tr("Default") + " (" + QString::fromStdWString(defaultOutputDevice->getConnectionName()) + " - " + QString::fromStdWString(defaultOutputDevice->getDeviceName()) + ")", QVariant::fromValue(shared_ptr<AbstractAPOInfo>()));

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
	// Ignored rather than Preferred: these sit in a bar capped at 250px, and a
	// combo's own size hint is its widest item plus the drop-down and whatever
	// padding the active skin gives it. That hint has always been larger than
	// the column can offer, so the layout laid them out at their minimum and
	// the bar clipped their right edge - which reads as the graph pane eating
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
		QString languageName;
		if (language == QLocale::AnyLanguage)
			languageName = autoLocale.nativeLanguageName();
		else
			languageName = QLocale(language).nativeLanguageName();
		if (languageName == "American English")
			languageName = "English";
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
	// silently; this poll is the only place the user learns about it. 20s is
	// far below the human noticing threshold for "eventually told me" and far
	// above any cost concern for an atomic-bool read.
	if (!VelopackBootstrap::isVelopackInstall())
		return;

	updateNoticeTimer = new QTimer(this);
	updateNoticeTimer->setInterval(20000);
	connect(updateNoticeTimer, &QTimer::timeout, this, [this]() {
		if (!VelopackBootstrap::hasPendingUpdate())
			return;
		updateNoticeTimer->stop();

		const QString version = QString::fromStdWString(VelopackBootstrap::pendingUpdateVersion());
		if (updateToast == nullptr)
			updateToast = new UpdateToast(centralWidget());
		updateToast->showMessage(version.isEmpty()
			? tr("An update has been downloaded and will be applied when you close the editor.")
			: tr("Update %0 has been downloaded and will be applied when you close the editor.").arg(version));
	});
	updateNoticeTimer->start();
}

MainWindow::~MainWindow()
= default;

void MainWindow::doChecks()
{
	if (!DeviceAPOInfo::checkProtectedAudioDG(false) || !DeviceAPOInfo::checkAPORegistration(false))
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
	{
		QStringList passthroughDevices;
		auto inspect = [&passthroughDevices](const QList<shared_ptr<AbstractAPOInfo>>& list)
		{
			for (const shared_ptr<AbstractAPOInfo>& info : list)
			{
				if (info == nullptr || !info->isInstalled())
					continue;
				AudioFormatProbe::Result r = AudioFormatProbe::probe(info->getDeviceGuid());
				if (AudioFormatProbe::isPassthrough(r.status))
				{
					QString label = QString::fromStdWString(info->getConnectionName())
						+ " - " + QString::fromStdWString(info->getDeviceName())
						+ "  (" + QString::fromStdWString(r.subtypeDescription)
						+ ", " + QString::number(r.containerBytes * 8) + "-bit)";
					passthroughDevices.append(label);
				}
			}
		};
		inspect(outputDevices);
		inspect(inputDevices);
		if (!passthroughDevices.isEmpty())
		{
			QMessageBox::warning(this,
				tr("EQ inactive on some devices"),
				tr("EqualizerAPO can only process IEEE_FLOAT 32/64-bit streams natively. The following installed devices currently use a different format, "
				   "so audio passes through them without any filter being applied:\n\n%0\n\nThis is not a crash — sound still reaches the device, but no EQ. "
				   "Switch the device's default format to a 32-bit IEEE_FLOAT one in Sound Settings if you need filtering on them.")
				.arg(passthroughDevices.join('\n')));
		}
	}

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
	UINT_PTR result = reinterpret_cast<UINT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
	if (result == SE_ERR_ACCESSDENIED)
		ShellExecuteW(nullptr, L"runas", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
