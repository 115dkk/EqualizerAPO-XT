/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Thedering

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

#include "stdafx.h"
#include <devices/DeviceAPOInfo.h>
#include <services/logging/Logging.h>
#include <services/registry/WindowsRegistry.h>
#include <platform/windows/WindowsVersion.h>
#include <services/windows/WindowsService.h>
#include <platform/windows/Win32Resource.h>
#include <QDir>
#include <QPropertyAnimation>
#include <QScreen>
#include <devices/AsioAPOInfo.h>
#include <devices/VoicemeeterAPOInfo.h>
#include "DeviceTestDialog.h"
#include "../version.h"
#include "DeviceListDelegate.h"
#include "DisclosureHeader.h"
#include "SkinButton.h"
#include "DeviceSelector.h"

namespace
{
	// The wait-time combo's rows against the share of the buffer period
	// the record stores (25, 50, 75).
	unsigned deadlinePercentForIndex(int index)
	{
		return index == 1 ? 50u : (index == 2 ? 75u : 25u);
	}

	int deadlineIndexForPercent(unsigned percent)
	{
		return percent == 50 ? 1 : (percent == 75 ? 2 : 0);
	}
}

DeviceSelector::DeviceSelector(QWidget* parent)
	: QDialog(parent)
{
	setupChrome();

	try
	{
		QTreeWidgetItem* outputNode = new QTreeWidgetItem(ui.deviceTreeWidget, QStringList(tr("Playback devices")));
		outputNode->setExpanded(true);
		std::vector<std::shared_ptr<AbstractAPOInfo>> outputDevices = DeviceAPOInfo::loadAllInfos(false);
		addDevices(outputDevices, outputNode);

		QTreeWidgetItem* inputNode = new QTreeWidgetItem(ui.deviceTreeWidget, QStringList(tr("Capture devices")));
		inputNode->setExpanded(true);
		inputNode->setData(0, DeviceListDelegate::InputSideRole, true);
		std::vector<std::shared_ptr<AbstractAPOInfo>> inputDevices = DeviceAPOInfo::loadAllInfos(true);
		addDevices(inputDevices, inputNode);
	}
	catch (const RegistryError& e)
	{
		QMessageBox::critical(this, tr("Error while accessing the registry"), QString::fromStdWString(e.getMessage()));
	}

	if (!WindowsVersion::isAtLeast(6, 3)) // Windows 8.1
	{
		ui.installModeComboBox->removeItem(2);
		ui.installModeComboBox->removeItem(1);
	}

	finishSetup();

	bool fixedAudioDG = !DeviceAPOInfo::checkProtectedAudioDG(true);
	bool fixedRegistration = !DeviceAPOInfo::checkAPORegistration(true);
	if (fixedAudioDG || fixedRegistration)
	{
		QMessageBox::information(this, tr("Info"), tr("A registry value that is required for the operation of Equalizer APO was not set correctly. "
			"This might have been caused by a driver installation or uninstallation. The value has been corrected. A reboot may be required so that the changes can take effect."));
		askForReboot = true;
	}
}

DeviceSelector::DeviceSelector(const std::vector<std::shared_ptr<AbstractAPOInfo>>& playback,
	const std::vector<std::shared_ptr<AbstractAPOInfo>>& capture, QWidget* parent)
	: QDialog(parent)
{
	setupChrome();

	QTreeWidgetItem* outputNode = new QTreeWidgetItem(ui.deviceTreeWidget, QStringList(tr("Playback devices")));
	outputNode->setExpanded(true);
	addDevices(playback, outputNode);

	QTreeWidgetItem* inputNode = new QTreeWidgetItem(ui.deviceTreeWidget, QStringList(tr("Capture devices")));
	inputNode->setExpanded(true);
	inputNode->setData(0, DeviceListDelegate::InputSideRole, true);
	addDevices(capture, inputNode);

	finishSetup();
}

void DeviceSelector::setupChrome()
{
	ui.setupUi(this);

	setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));

	// Audit #250 F019: one display-version rule for all binaries (version.h).
	QString version = QString::fromStdWString(eapoDisplayVersionW());
	setWindowTitle(QString("Equalizer APO %0 Device Selector").arg(version));

	// The device list renders through the active skin's painter; the tree
	// keeps the data and behaviour. No native decorations or header - the
	// painted rows carry the whole presentation.
	ui.deviceTreeWidget->setItemDelegate(new DeviceListDelegate(ui.deviceTreeWidget));
	ui.deviceTreeWidget->setRootIsDecorated(false);
	// No indentation: every skin draws its own left-edge structure (bus
	// rails, gutters, port lanes), and a branch strip would sit outside the
	// delegate rect where the view fills palette Highlight on selection,
	// producing a detached selection bar.
	ui.deviceTreeWidget->setIndentation(0);
	// Sections fold on a single click (the delegate consumes the press) and
	// the delegate also folds on the double-click's second press; leave the
	// view's own double-click expansion off so the two never fight and every
	// rapid click toggles exactly once.
	ui.deviceTreeWidget->setExpandsOnDoubleClick(false);
	QPalette listPalette = ui.deviceTreeWidget->palette();
	listPalette.setBrush(QPalette::Highlight, Qt::transparent);
	ui.deviceTreeWidget->setPalette(listPalette);
	ui.deviceTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

	// Skin-painted dialog buttons; the box only supplies the roles. The
	// title string reuses the .ui's original translation context.
	okButton = new SkinButton(tr("OK"), true, this);
	cancelButton = new SkinButton(tr("Cancel"), false, this);
	ui.buttonBox->addButton(okButton, QDialogButtonBox::AcceptRole);
	ui.buttonBox->addButton(cancelButton, QDialogButtonBox::RejectRole);
	ui.troubleshootingHeader->setTitle(QCoreApplication::translate("DeviceSelectorClass",
		"Troubleshooting options (only use in case of problems)"));
}

void DeviceSelector::finishSetup()
{
	// Connected only after the devices are in place: itemChanged fires for
	// every setData during population, and updateList would dereference the
	// not-yet-stored device info.
	connect(ui.deviceTreeWidget, &QTreeWidget::itemChanged, this, &DeviceSelector::onDeviceToggled);
	connect(ui.deviceTreeWidget, &QTreeWidget::itemSelectionChanged, this, &DeviceSelector::onDeviceSelectionChanged);
	connect(ui.deviceTreeWidget, &QTreeWidget::customContextMenuRequested, this, &DeviceSelector::onDeviceContextMenuRequested);
	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(this, &QDialog::accepted, this, &DeviceSelector::onDialogAccepted);
	connect(this, &QDialog::rejected, this, &DeviceSelector::onDialogRejected);
	connect(ui.copyDeviceCommandAction, &QAction::triggered, this, &DeviceSelector::onCopyDeviceCommandClicked);
	connect(ui.troubleshootingHeader, &DisclosureHeader::toggled, this, &DeviceSelector::onTroubleShootingToggled);
	connect(ui.installPreMixCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.installPostMixCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.useOriginalAPOPreMixCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.useOriginalAPOPostMixCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.installModeComboBox, QOverload<int>::of(&QComboBox::activated), this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.allowSilentBufferCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.exclusiveModeEqCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.autoCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.asioSyncCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.asioDeadlineComboBox, QOverload<int>::of(&QComboBox::activated), this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.asioAutoStartCheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);
	connect(ui.asioHost32CheckBox, &QCheckBox::clicked, this, &DeviceSelector::onTroubleShootingOptionChanged);

	updateButtons();

	// The disclosure starts folded; the header never fired a signal yet, so
	// hide the panel directly instead of replaying the slide.
	ui.stackedWidget->setVisible(false);
	adjustSize();

	// adjustSize() settles on the tree's minimum, which opens the dialog too
	// narrow to read the device names at a glance (reported after a clean
	// install, where nothing has been resized yet). Open at a size the list
	// reads comfortably at, as far as the screen allows.
	QSize wanted = size().expandedTo(QSize(760, 640));
	if (const QScreen* onScreen = screen())
		wanted = wanted.boundedTo(onScreen->availableGeometry().size() - QSize(40, 80));
	resize(wanted);

	// workaround for Qt 6 to not initially have scrollbars despite correct dialog size
	ui.deviceTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QTimer::singleShot(0, [&] {ui.deviceTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); });
}

void DeviceSelector::addDevices(const std::vector<std::shared_ptr<AbstractAPOInfo>>& devices, QTreeWidgetItem* parentNode)
{
	for (const std::shared_ptr<AbstractAPOInfo>& apoInfo : devices)
	{
		VoicemeeterAPOInfo* voicemeeterInfo = dynamic_cast<VoicemeeterAPOInfo*>(apoInfo.get());
		bool checked = false;
		if (apoInfo->isInstalled())
		{
			if (voicemeeterInfo != nullptr && !voicemeeterInfo->isVoicemeeterInstalled())
				checked = false;
			else
				checked = true;
		}

		QTreeWidgetItem* item = new QTreeWidgetItem(parentNode,
			QStringList(QString::fromStdWString(apoInfo->getConnectionName())));
		// The info pointer first: everything downstream of itemChanged reads it.
		item->setData(0, Qt::UserRole, QVariant::fromValue(apoInfo));
		item->setData(0, DeviceListDelegate::DeviceNameRole, QString::fromStdWString(apoInfo->getDeviceName()));
		item->setData(0, DeviceListDelegate::StateTextRole, getStateText(apoInfo, checked));
		item->setData(0, DeviceListDelegate::InstalledRole, apoInfo->isInstalled());
		item->setData(0, DeviceListDelegate::DefaultDeviceRole, apoInfo->isDefaultDevice());
		item->setData(0, DeviceListDelegate::UnavailableRole, apoInfo->isDisabled() || apoInfo->isUnplugged());
		item->setData(0, DeviceListDelegate::InputSideRole, apoInfo->isInput());

		item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
}

void DeviceSelector::previewHoverDevice(int sectionRow, int deviceRow)
{
	QTreeWidgetItem* section = ui.deviceTreeWidget->topLevelItem(sectionRow);
	DeviceListDelegate* delegate = qobject_cast<DeviceListDelegate*>(ui.deviceTreeWidget->itemDelegate());
	if (section == nullptr || delegate == nullptr || deviceRow >= section->childCount())
		return;
	delegate->setForcedHover(ui.deviceTreeWidget->indexFromItem(section->child(deviceRow)));
}

void DeviceSelector::previewSelectDevice(int sectionRow, int deviceRow)
{
	QTreeWidgetItem* section = ui.deviceTreeWidget->topLevelItem(sectionRow);
	if (section == nullptr || deviceRow >= section->childCount())
		return;
	ui.deviceTreeWidget->setCurrentItem(section->child(deviceRow));
}

void DeviceSelector::previewCheckDevice(int sectionRow, int deviceRow)
{
	QTreeWidgetItem* section = ui.deviceTreeWidget->topLevelItem(sectionRow);
	if (section == nullptr || deviceRow >= section->childCount())
		return;
	section->child(deviceRow)->setCheckState(0, Qt::Checked);
}

void DeviceSelector::previewRemoveBuffer()
{
	// The preview roster has no wrapper record behind its ASIO rows, so a
	// click would be undone by the selection refresh that follows it. Pin
	// the view state the way a real record's click leaves it.
	ui.asioSyncCheckBox->setChecked(true);
	showAsioWaitTime(true);
}

void DeviceSelector::showAsioWaitTime(bool shown)
{
	ui.asioWaitLabel->setVisible(shown);
	ui.asioDeadlineComboBox->setVisible(shown);
}

void DeviceSelector::previewOpenTroubleshooting()
{
	ui.troubleshootingHeader->setChecked(true);
	// Skip the slide for deterministic shots: pin the panel at full height.
	if (troubleshootingSlide != nullptr)
		troubleshootingSlide->stop();
	ui.stackedWidget->setVisible(true);
	ui.stackedWidget->setMaximumHeight(QWIDGETSIZE_MAX);
}

void DeviceSelector::onDeviceSelectionChanged()
{
	updateButtons();
}

void DeviceSelector::onDeviceToggled(QTreeWidgetItem* item)
{
	updateList(item);
	updateButtons();
}

void DeviceSelector::onDeviceContextMenuRequested(const QPoint& pos)
{
	QMenu menu(this);
	menu.addAction(ui.copyDeviceCommandAction);
	menu.exec(ui.deviceTreeWidget->mapToGlobal(pos));
}

void DeviceSelector::onDialogAccepted()
{
	bool deviceUpdated = false;

	for (int index = 0; index < ui.deviceTreeWidget->topLevelItemCount(); index++)
	{
		QTreeWidgetItem* topItem = ui.deviceTreeWidget->topLevelItem(index);
		for (int i = 0; i < topItem->childCount(); i++)
		{
			QTreeWidgetItem* item = topItem->child(i);
			std::shared_ptr<AbstractAPOInfo> info = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
			bool checked = item->checkState(0) == Qt::Checked;

			try
			{
				const DeviceAPOInfo* deviceInfo = dynamic_cast<DeviceAPOInfo*>(info.get());
				if (checked && !info->isInstalled())
				{
					info->install();
					if (deviceInfo != nullptr)
						deviceUpdated = true;
				}
				else if (!checked && info->isInstalled())
				{
					info->uninstall();
					if (deviceInfo != nullptr)
						deviceUpdated = true;
				}
				else if (checked && (info->canBeUpgraded() || info->hasChanges() || info->isEnhancementsDisabled()))
				{
					info->reinstall();
					if (deviceInfo != nullptr)
						deviceUpdated = true;
				}
			}
			catch (const RegistryError& e)
			{
				// The operation put the endpoint back before throwing, and its
				// report is already in DeviceSelector.log. Two things are added
				// here: where to find that log, and the one case where the
				// endpoint was *not* put back, which the user has to know about
				// before they try again.
				QString message = QString::fromStdWString(e.getMessage());
				const DeviceInstallReport& report = info->getLastOperationReport();
				if (report.leftInconsistent())
				{
					message += QLatin1String("\n\n") + tr("Undoing the change did not finish either, so this "
						"device may be left partly changed. Reboot before trying again, and see the log for details.");
				}
				else if (report.operation != DeviceInstallReport::Operation::None)
				{
					message += QLatin1String("\n\n") + tr("The device was left as it was before.");
				}
				message += QLatin1String("\n\n") + tr("Details are in %1.")
					.arg(QDir::toNativeSeparators(QString::fromStdWString(Logging::currentPath())));
				QMessageBox::critical(this, tr("Error while accessing the registry"), message);
			}
		}
	}

	VoicemeeterAPOInfo::ensureVoicemeeterClientRunning();

	finish(deviceUpdated);
}

void DeviceSelector::onDialogRejected()
{
	if (hasUpgrades())
	{
		if (QMessageBox::warning(this, tr("Upgrades available"), tr("The APO installation of some devices should be upgraded. Do you really want to cancel?"),
			QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No)) == QMessageBox::No)
			return;
	}

	finish(false);
}

void DeviceSelector::finish(bool deviceUpdated)
{
	int dialogResult = 0;
	if (QCoreApplication::instance()->arguments().contains("/i")
		|| deviceUpdated || askForReboot)
	{
		DeviceTestDialog testDialog;
		dialogResult = testDialog.exec();
	}

	int returnCode = 0;
	if (QCoreApplication::instance()->arguments().contains("/i"))
	{
		QMessageBox::information(this, tr("Info"), tr("This dialog can be reopened anytime by launching Device Selector from the start menu."));
		if (dialogResult == -1)
			returnCode = 1;
	}
	else if (dialogResult == -1)
	{
		if (QMessageBox::question(this, tr("Reboot"), tr("To apply the changes, Windows should be rebooted. Reboot now?")) == QMessageBox::Yes)
		{
			winutil::UniqueHandle tokenHandle;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
				tokenHandle.put()))
			{
				LUID luid;
				if (LookupPrivilegeValue(nullptr, SE_SHUTDOWN_NAME, &luid))
				{
					TOKEN_PRIVILEGES tp;
					tp.PrivilegeCount = 1;
					tp.Privileges[0].Luid = luid;
					tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

					if (AdjustTokenPrivileges(tokenHandle.get(), FALSE, &tp,
						sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
						InitiateShutdownW(nullptr, nullptr, 0, SHUTDOWN_RESTART | SHUTDOWN_GRACE_OVERRIDE, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_MAINTENANCE);
				}
			}
		}
	}

	QCoreApplication::exit(returnCode);
}

void DeviceSelector::onCopyDeviceCommandClicked()
{
	QString command = "Device: ";

	QList<QTreeWidgetItem*> list = ui.deviceTreeWidget->selectedItems();

	bool first = true;
	for (QTreeWidgetItem* item : list)
	{
		if (item->childCount() != 0)
			continue;

		if (first)
			first = false;
		else
			command += "; ";

		std::shared_ptr<AbstractAPOInfo> info = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
		command += QString::fromStdWString(info->getDeviceString()).replace(';', ' ');
	}

	QClipboard* clipboard = QGuiApplication::clipboard();
	clipboard->setText(command);
}

void DeviceSelector::onTroubleShootingToggled(bool on)
{
	// Disclosure slide: the panel's maximumHeight sweeps between 0 and its
	// natural height. 160ms OutCubic reads as a fold without ever feeling
	// like waiting; the indicator is restyled as a fold chevron in
	// main.cpp's theme sheet.
	if (troubleshootingSlide == nullptr)
	{
		troubleshootingSlide = new QPropertyAnimation(ui.stackedWidget, "maximumHeight", this);
		troubleshootingSlide->setDuration(160);
		troubleshootingSlide->setEasingCurve(QEasingCurve::OutCubic);
		connect(troubleshootingSlide, &QPropertyAnimation::finished, this, [this]() {
			if (!ui.troubleshootingHeader->isChecked())
				ui.stackedWidget->setVisible(false);
			// Release the clamp so future layout changes (translations, DPI)
			// keep sizing the open panel naturally.
			ui.stackedWidget->setMaximumHeight(QWIDGETSIZE_MAX);
		});
	}

	troubleshootingSlide->stop();
	if (on)
	{
		ui.stackedWidget->setMaximumHeight(0);
		ui.stackedWidget->setVisible(true);
		troubleshootingSlide->setStartValue(0);
		troubleshootingSlide->setEndValue(ui.stackedWidget->sizeHint().height());
	}
	else
	{
		troubleshootingSlide->setStartValue(ui.stackedWidget->height());
		troubleshootingSlide->setEndValue(0);
	}
	troubleshootingSlide->start();
}

void DeviceSelector::onTroubleShootingOptionChanged()
{
	QList<QTreeWidgetItem*> list = ui.deviceTreeWidget->selectedItems();
	for (QTreeWidgetItem* item : list)
	{
		if (item->childCount() != 0)
			continue;

		std::shared_ptr<AbstractAPOInfo> info = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
		DeviceAPOInfo* deviceInfo = dynamic_cast<DeviceAPOInfo*>(info.get());
		if (deviceInfo != nullptr)
		{
			const QObject* sender = QObject::sender();
			if (sender == ui.installPreMixCheckBox)
				deviceInfo->getSelectedInstallState().installPreMix = ui.installPreMixCheckBox->isChecked();
			else if (sender == ui.installPostMixCheckBox)
				deviceInfo->getSelectedInstallState().installPostMix = ui.installPostMixCheckBox->isChecked();
			else if (sender == ui.useOriginalAPOPreMixCheckBox)
				deviceInfo->getSelectedInstallState().useOriginalAPOPreMix = ui.useOriginalAPOPreMixCheckBox->isChecked();
			else if (sender == ui.useOriginalAPOPostMixCheckBox)
				deviceInfo->getSelectedInstallState().useOriginalAPOPostMix = ui.useOriginalAPOPostMixCheckBox->isChecked();
			else if (sender == ui.installModeComboBox)
				deviceInfo->getSelectedInstallState().installMode = (DeviceAPOInfo::InstallMode)ui.installModeComboBox->currentIndex();
			else if (sender == ui.allowSilentBufferCheckBox)
				deviceInfo->getSelectedInstallState().allowSilentBufferModification = ui.allowSilentBufferCheckBox->isChecked();
			else if (sender == ui.autoCheckBox)
				deviceInfo->getSelectedInstallState().autoAdjust = ui.autoCheckBox->isChecked();
			else if (sender == ui.exclusiveModeEqCheckBox)
				deviceInfo->getSelectedInstallState().exclusiveModeEq = ui.exclusiveModeEqCheckBox->isChecked();
		}
		AsioAPOInfo* asioInfo = dynamic_cast<AsioAPOInfo*>(info.get());
		if (asioInfo != nullptr)
		{
			const QObject* const source = QObject::sender();
			if (source == ui.asioSyncCheckBox)
				asioInfo->setSynchronous(ui.asioSyncCheckBox->isChecked());
			else if (source == ui.asioDeadlineComboBox)
				asioInfo->setDeadlinePercent(deadlinePercentForIndex(ui.asioDeadlineComboBox->currentIndex()));
			else if (source == ui.asioAutoStartCheckBox)
				asioInfo->setAutoStart(ui.asioAutoStartCheckBox->isChecked());
			else if (source == ui.asioHost32CheckBox)
				asioInfo->setHost32(ui.asioHost32CheckBox->isChecked());
		}
		// The wait time is the buffer removal's own detail: it unfolds beside
		// the checkbox while the buffer is removed and is gone otherwise.
		showAsioWaitTime(ui.asioSyncCheckBox->isEnabled() && ui.asioSyncCheckBox->isChecked());

		updateList(item);
	}

	updateButtons();
}

void DeviceSelector::updateList(QTreeWidgetItem* item)
{
	std::shared_ptr<AbstractAPOInfo> apoInfo = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
	bool checked = item->checkState(0) == Qt::Checked;

	item->setData(0, DeviceListDelegate::StateTextRole, getStateText(apoInfo, checked));
}

void DeviceSelector::updateButtons()
{
	bool changed = isChanged();
	if (changed || !isAnySelected())
	{
		okButton->setVisible(true);
		okButton->setEnabled(changed);
		cancelButton->setText(tr("Cancel"));
	}
	else
	{
		okButton->setVisible(false);
		cancelButton->setText(tr("Close"));
	}

	QList<QTreeWidgetItem*> list = ui.deviceTreeWidget->selectedItems();
	bool noGroupsSelected = !list.isEmpty();
	for (QTreeWidgetItem* item : list)
	{
		if (item->childCount() != 0)
		{
			noGroupsSelected = false;
			break;
		}
	}

	ui.copyDeviceCommandAction->setEnabled(noGroupsSelected);

	bool enable = false;
	bool isInput = false;
	bool hasOriginalAPOPreMix = true;
	bool hasOriginalAPOPostMix = true;
	bool asioSelected = false;
	bool asioSynchronous = false;
	unsigned asioDeadlinePercent = 25;
	bool asioAutoStart = false;
	bool asioHost32 = false;
	bool asioCanHost32 = true;   // the preview roster has the 32-bit wrapper
	DeviceAPOInfo::InstallState installState;
	if (noGroupsSelected && list.size() == 1)
	{
		QTreeWidgetItem* item = list[0];
		enable = item->checkState(0) == Qt::Checked;

		std::shared_ptr<AbstractAPOInfo> apoInfo = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
		DeviceAPOInfo* deviceApoInfo = dynamic_cast<DeviceAPOInfo*>(apoInfo.get());
		if (deviceApoInfo != nullptr)
		{
			isInput = deviceApoInfo->isInput();
			hasOriginalAPOPreMix = deviceApoInfo->getOriginalAPOPreMix() != L"";
			hasOriginalAPOPostMix = deviceApoInfo->getOriginalAPOPostMix() != L"";
			installState = deviceApoInfo->getSelectedInstallState();
		}
		// Keyed on the transport label, not the class, so the preview roster
		// (plain preview records marked ASIO) shows the page in the gallery.
		asioSelected = apoInfo->getTransportLabel() == L"ASIO";
		const AsioAPOInfo* asioInfo = dynamic_cast<const AsioAPOInfo*>(apoInfo.get());
		if (asioInfo != nullptr)
		{
			asioSynchronous = asioInfo->isSynchronous();
			asioDeadlinePercent = asioInfo->getDeadlinePercent();
			asioAutoStart = asioInfo->isAutoStart();
			asioHost32 = asioInfo->isHost32();
			asioCanHost32 = asioInfo->canHost32();
		}
	}

	ui.preMixLabel->setEnabled(enable);
	ui.postMixLabel->setEnabled(enable && !isInput);
	ui.installPreMixCheckBox->setEnabled(enable);
	ui.installPostMixCheckBox->setEnabled(enable && !isInput);
	ui.useOriginalAPOPreMixCheckBox->setEnabled(enable && hasOriginalAPOPreMix && installState.installPreMix);
	ui.useOriginalAPOPostMixCheckBox->setEnabled(enable && !isInput && hasOriginalAPOPostMix && installState.installPostMix);
	ui.installModeComboBox->setEnabled(enable);
	ui.allowSilentBufferCheckBox->setEnabled(enable);
	ui.exclusiveModeEqCheckBox->setEnabled(enable);
	// Page 0: nothing to say. Page 1: an endpoint's APO chain. Page 2: an
	// ASIO target's options.
	ui.stackedWidget->setCurrentIndex(!enable ? 0 : (asioSelected ? 2 : 1));
	const bool asioEnabled = enable && asioSelected;
	ui.asioSyncCheckBox->setEnabled(asioEnabled);
	ui.asioSyncCheckBox->setChecked(asioSynchronous);
	ui.asioDeadlineComboBox->setCurrentIndex(deadlineIndexForPercent(asioDeadlinePercent));
	showAsioWaitTime(asioEnabled && asioSynchronous);
	ui.asioAutoStartCheckBox->setEnabled(asioEnabled);
	ui.asioAutoStartCheckBox->setChecked(asioAutoStart);
	ui.asioHost32CheckBox->setEnabled(asioEnabled && asioCanHost32);
	ui.asioHost32CheckBox->setChecked(asioHost32 && asioCanHost32);

	ui.installPreMixCheckBox->setChecked(installState.installPreMix);
	ui.installPostMixCheckBox->setChecked(installState.installPostMix);
	ui.useOriginalAPOPreMixCheckBox->setChecked(installState.useOriginalAPOPreMix && hasOriginalAPOPreMix);
	ui.useOriginalAPOPostMixCheckBox->setChecked(installState.useOriginalAPOPostMix && hasOriginalAPOPostMix);

	if (WindowsVersion::isAtLeast(6, 3)) // Windows 8.1
		ui.installModeComboBox->setCurrentIndex(installState.installMode);

	ui.allowSilentBufferCheckBox->setChecked(installState.allowSilentBufferModification);
	ui.exclusiveModeEqCheckBox->setChecked(installState.exclusiveModeEq);
	ui.autoCheckBox->setChecked(installState.autoAdjust);
}

bool DeviceSelector::isAnySelected()
{
	bool anySelected = false;

	for (int index = 0; index < ui.deviceTreeWidget->topLevelItemCount(); index++)
	{
		QTreeWidgetItem* topItem = ui.deviceTreeWidget->topLevelItem(index);
		for (int i = 0; i < topItem->childCount(); i++)
		{
			QTreeWidgetItem* item = topItem->child(i);
			if (item->checkState(0) == Qt::Checked)
			{
				anySelected = true;
				break;
			}
		}
	}

	return anySelected;
}

bool DeviceSelector::isChanged()
{
	bool changed = false;

	for (int index = 0; index < ui.deviceTreeWidget->topLevelItemCount(); index++)
	{
		QTreeWidgetItem* topItem = ui.deviceTreeWidget->topLevelItem(index);
		for (int i = 0; i < topItem->childCount(); i++)
		{
			QTreeWidgetItem* item = topItem->child(i);
			std::shared_ptr<AbstractAPOInfo> apoInfo = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
			bool checked = item->checkState(0) == Qt::Checked;
			if (checked != apoInfo->isInstalled()
				|| checked && apoInfo->isInstalled() && (apoInfo->canBeUpgraded() || apoInfo->hasChanges() || apoInfo->isEnhancementsDisabled()))
			{
				changed = true;
				break;
			}
		}
	}

	return changed;
}

bool DeviceSelector::hasUpgrades()
{
	bool hasUpgrades = false;

	for (int index = 0; index < ui.deviceTreeWidget->topLevelItemCount(); index++)
	{
		QTreeWidgetItem* topItem = ui.deviceTreeWidget->topLevelItem(index);
		for (int i = 0; i < topItem->childCount(); i++)
		{
			QTreeWidgetItem* item = topItem->child(i);
			std::shared_ptr<AbstractAPOInfo> apoInfo = item->data(0, Qt::UserRole).value<std::shared_ptr<AbstractAPOInfo>>();
			bool checked = item->checkState(0) == Qt::Checked;
			if (checked && apoInfo->isInstalled() && (apoInfo->canBeUpgraded() || apoInfo->isEnhancementsDisabled()))
			{
				hasUpgrades = true;
				break;
			}
		}
	}

	return hasUpgrades;
}

QString DeviceSelector::getStateText(const std::shared_ptr<AbstractAPOInfo>& apoInfo, bool checked)
{
	QString state;
	if (checked && !apoInfo->isInstalled())
		state = tr("APO will be installed");
	else if (!checked && apoInfo->isInstalled())
		state = tr("APO will be uninstalled");
	else if (apoInfo->isInstalled() && apoInfo->canBeUpgraded())
		state = tr("APO will be upgraded");
	else if (apoInfo->isInstalled() && apoInfo->hasChanges())
		state = tr("APO installation will be changed");
	else if (apoInfo->isInstalled() && apoInfo->isEnhancementsDisabled())
		state = tr("Audio enhancements will be enabled");
	else if (apoInfo->isInstalled())
		state = tr("APO is already installed");
	else
		state = tr("APO can be installed");

	VoicemeeterAPOInfo* voicemeeterInfo = dynamic_cast<VoicemeeterAPOInfo*>(apoInfo.get());
	if (voicemeeterInfo != nullptr && !voicemeeterInfo->isVoicemeeterInstalled())
		state += ", " + tr("Voicemeeter was uninstalled");
	else if (apoInfo->isDefaultDevice())
		state += ", " + tr("Default device");

	// The transport (ASIO) is the row's leading word already, the way an
	// endpoint's connection is; the state line does not say it again.

	if (apoInfo->isDisabled())
		state += ", " + tr("Disabled");
	if (apoInfo->isUnplugged())
		state += ", " + tr("Unplugged");

	return state;
}
