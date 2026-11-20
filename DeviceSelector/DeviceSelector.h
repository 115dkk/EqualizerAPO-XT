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

#pragma once

#include <asio/EntryOptions.h>
#include <devices/AbstractAPOInfo.h>
#include <QtWidgets/QDialog>
#include "DeviceTestDialog.h"
#include "ui_DeviceSelector.h"

class QPropertyAnimation;
class SkinButton;

class DeviceSelector : public QDialog
{
	Q_OBJECT

public:
	DeviceSelector(QWidget* parent = nullptr);
	// Preview construction for the skin shot harness: canned device lists,
	// no registry access, no repair prompts.
	DeviceSelector(const std::vector<std::shared_ptr<AbstractAPOInfo>>& playback,
		const std::vector<std::shared_ptr<AbstractAPOInfo>>& capture, QWidget* parent = nullptr);
	void addDevices(const std::vector<std::shared_ptr<AbstractAPOInfo>>& devices, QTreeWidgetItem* parentNode);

	// Skin shot harness hooks (offscreen states without a real pointer).
	void previewHoverDevice(int sectionRow, int deviceRow);
	void previewSelectDevice(int sectionRow, int deviceRow);
	void previewCheckDevice(int sectionRow, int deviceRow);
	void previewOpenTroubleshooting();
	void previewRemoveBuffer();

	void previewAsioEntry();
	void previewReceive();

private:
	void showAsioWaitTime(bool shown);
	void placeAsioOptions(bool underAsioEntry);
	bool readAsioOption(const QObject* sender, eapo::asio::EntryOptions& options) const;
	void setupChrome();
	void finishSetup();
	void onDeviceSelectionChanged();
	void onDeviceToggled(QTreeWidgetItem* item);
	void onDeviceContextMenuRequested(const QPoint& pos);
	void onDialogAccepted();
	void onDialogRejected();
	void finish(bool deviceUpdated);
	void onCopyDeviceCommandClicked();
	void onTroubleShootingToggled(bool on);
	void onTroubleShootingOptionChanged();
	void updateList(QTreeWidgetItem* item);
	void updateButtons();
	bool isAnySelected();
	bool isChanged();
	bool hasUpgrades();
	QString getStateText(const std::shared_ptr<AbstractAPOInfo>& apoInfo, bool checked);

	Ui::DeviceSelectorClass ui;
	bool askForReboot = false;
	QVector<DeviceTestDialog::InstallFailure> installFailures;
	// The dialog buttons are skin-painted (see SkinButton); QDialogButtonBox
	// only supplies the roles, so button() lookups by standard id are gone.
	SkinButton* okButton = nullptr;
	SkinButton* cancelButton = nullptr;
	// Slide animation behind the troubleshooting disclosure (lazily created
	// in onTroubleShootingToggled).
	QPropertyAnimation* troubleshootingSlide = nullptr;
	// The folding part of the disclosure: the per-device stack and, below it,
	// the options that apply to every device. Built in setupChrome.
	QWidget* troubleshootingPanel = nullptr;
	// Engine-wide: trims the silent end of convolution impulse responses
	// (IgnoreConvolutionSilence under APP_REGPATH). The stored value is what
	// the registry held when the dialog opened, so OK writes only a change.
	QCheckBox* ignoreConvolutionSilenceCheckBox = nullptr;
	bool ignoreConvolutionSilenceStored = true;
	// Engine-wide: runs the convolver units of Convolution and MultiConvolution
	// on several cores (ParallelConvolution under APP_REGPATH). Stored like the
	// option above.
	QCheckBox* parallelConvolutionCheckBox = nullptr;
	bool parallelConvolutionStored = true;
	// Per endpoint, under "Send": resample what other endpoints send here
	// when their sample rate differs (InstallState::resampleReceived).
	QCheckBox* resampleReceivedCheckBox = nullptr;
};

Q_DECLARE_METATYPE(std::shared_ptr<AbstractAPOInfo>)
