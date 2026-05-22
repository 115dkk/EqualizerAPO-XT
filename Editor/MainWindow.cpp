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
	: QMainWindow(parent), ui(new Ui::MainWindow), configDir(configDir)
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

	LogHelper::set(stderr, true, false, false);

	QString version = QString("%0.%1").arg(MAJOR).arg(MINOR);
	if (REVISION != 0)
		version += QString(".%0").arg(REVISION);
	setWindowTitle(tr("Equalizer APO %0 Configuration Editor").arg(version));

	ui->mainToolBar->setObjectName(QStringLiteral("MainToolBar"));

	QWidget* spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setFixedWidth(10);
	ui->mainToolBar->addWidget(spacer);

	instantModeCheckBox = new QCheckBox(tr("Instant mode"));
	instantModeCheckBox->setObjectName(QStringLiteral("InstantModeCheckBox"));
	instantModeCheckBox->setChecked(true);
	instantModeCheckBox->setToolTip(tr("Changes are saved immediately"));
	connect(instantModeCheckBox, SIGNAL(toggled(bool)), this, SLOT(instantModeEnabled(bool)));
	ui->mainToolBar->addWidget(instantModeCheckBox);

	spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->mainToolBar->addWidget(spacer);

	QLabel* deviceLabel = new QLabel(tr("Device"));
	deviceLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	ui->mainToolBar->addWidget(deviceLabel);

	deviceComboBox = new QComboBox;
	deviceComboBox->setObjectName(QStringLiteral("ToolBarComboBox"));
	connect(deviceComboBox, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::deviceSelected);
	ui->mainToolBar->addWidget(deviceComboBox);

	spacer = new QWidget;
	spacer->setObjectName(QStringLiteral("ToolBarSpacer"));
	spacer->setFixedWidth(10);
	ui->mainToolBar->addWidget(spacer);

	QLabel* channelLabel = new QLabel(tr("Channels"));
	channelLabel->setObjectName(QStringLiteral("ToolBarLabel"));
	ui->mainToolBar->addWidget(channelLabel);

	channelConfigurationComboBox = new QComboBox;
	channelConfigurationComboBox->setObjectName(QStringLiteral("ToolBarComboBox"));
	channelConfigurationComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	ui->mainToolBar->addWidget(channelConfigurationComboBox);

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

	analysisPlotScene = new AnalysisPlotScene(ui->graphicsView);
	ui->graphicsView->setScene(analysisPlotScene);
	eqGraphView = new EqGraphView(ui->dockWidgetContents);
	eqGraphView->setObjectName(QStringLiteral("ModernAnalysisGraph"));
	ui->analysisDockLayout->insertWidget(1, eqGraphView, 1);
	ui->graphicsView->hide();

	ui->analysisControlBar->setObjectName(QStringLiteral("analysisControlBar"));
	ui->analysisControlBar->setAttribute(Qt::WA_StyledBackground, true);
	for (QLabel* label : { ui->startFromLabel, ui->analysisChannelLabel, ui->resolutionLabel })
		label->setObjectName(QStringLiteral("AnalysisFormLabel"));
	for (QComboBox* combo : { ui->startFromComboBox, ui->analysisChannelComboBox })
		combo->setObjectName(QStringLiteral("AnalysisFormCombo"));
	ui->resolutionSpinBox->setObjectName(QStringLiteral("AnalysisFormSpin"));
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

	analysisThread = new AnalysisThread;
	analysisThread->start();
	connect(analysisThread, SIGNAL(analysisFinished()), this, SLOT(updateAnalysisPanel()));

	QLocale autoLocale = QLocale::system();
	if (autoLocale.language() != QLocale::German && autoLocale.language() != QLocale::Chinese && autoLocale.language() != QLocale::French && autoLocale.language() != QLocale::Korean)
		autoLocale = QLocale("en");
	QLocale::Language languages[] = {QLocale::AnyLanguage, QLocale::English, QLocale::German, QLocale::Chinese, QLocale::French, QLocale::Korean};
	for (size_t i = 0; i < sizeof(languages) / sizeof(QLocale::Language); i++)
	{
		QLocale::Language language = languages[i];
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
}

MainWindow::~MainWindow()
{
	delete ui;

	delete analysisThread;
}

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

void MainWindow::runDeviceSelector()
{
	// cannot use QProcess::startDetached because of UAC
	wstring file = (QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/DeviceSelector.exe")).toStdWString();
	UINT_PTR result = reinterpret_cast<UINT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
	if (result == SE_ERR_ACCESSDENIED)
		ShellExecuteW(nullptr, L"runas", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
