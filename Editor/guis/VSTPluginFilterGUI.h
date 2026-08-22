/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include <memory>
#include <optional>
#include <QElapsedTimer>
#include "Editor/IFilterGUI.h"
#include "Editor/widgets/cards/VSTSlotFillModel.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

class QCheckBox;

namespace Ui {
class VSTPluginFilterGUI;
}

class VSTPluginFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap,
		bool stereoInput = false, const std::optional<VST3BusContract>& busContract = std::nullopt,
		std::vector<std::wstring> inputChannels = {}, std::vector<std::wstring> outputChannels = {});
	~VSTPluginFilterGUI() override;

	void store(QString& command, QString& parameters) override;
	void configureSelectedChannels(std::vector<std::wstring>& selectedChannels) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;
	void onAutomate();
	void onSizeWindow(int w, int h);

private slots:
	void on_openPanelButton_clicked();
	void applyDialog();
	void autoApplyToggled(bool checked);
	void on_pathLineEdit_editingFinished();
	void on_selectButton_clicked();
	void on_embedAction_toggled(bool checked);
	void stereoInputToggled(bool checked);
	void busLayoutPicked();
	void fillToggleClicked(bool checked);
	void on_idle();

private:
	void initPlugin();
	bool embedPlugin();
	void updatePermissionWarning();
	void updateBusControls();
	void updateFillRows();
	void rebuildFillRow(bool output);

	std::unique_ptr<Ui::VSTPluginFilterGUI> ui;
	std::shared_ptr<VSTPluginLibrary> library;
	std::unique_ptr<VSTPluginInstance> effect;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool embedded = false;
	bool autoApplyDialog = false;
	bool stereoInput = false;
	std::optional<VST3BusContract> busContract;
	// Per-slot channel fill for the forced layouts, edited by the two plain
	// combo rows below the bus dropdowns. A side's list drops when that
	// side's layout changes, because the slot count no longer matches.
	std::vector<std::wstring> inputChannels;
	std::vector<std::wstring> outputChannels;
	VSTSlotFillModel fillModel;
	bool fillCollapsed = false;
	bool fillCollapsedFromPrefs = false;
	QWidget* inputFillRow = nullptr;
	QWidget* outputFillRow = nullptr;
	QCheckBox* fillToggle = nullptr;
	QAction* stereoInputAction = nullptr;
	QElapsedTimer lastReadTimer;
};
