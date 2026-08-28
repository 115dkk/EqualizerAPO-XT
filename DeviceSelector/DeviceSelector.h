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

#include <devices/AbstractAPOInfo.h>
#include <QtWidgets/QDialog>
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

private:
	void showAsioWaitTime(bool shown);
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
	// The dialog buttons are skin-painted (see SkinButton); QDialogButtonBox
	// only supplies the roles, so button() lookups by standard id are gone.
	SkinButton* okButton = nullptr;
	SkinButton* cancelButton = nullptr;
	// Slide animation behind the troubleshooting disclosure (lazily created
	// in onTroubleShootingToggled).
	QPropertyAnimation* troubleshootingSlide = nullptr;
};

Q_DECLARE_METATYPE(std::shared_ptr<AbstractAPOInfo>)
