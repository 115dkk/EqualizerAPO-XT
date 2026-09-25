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
#include "Editor/IFilterGUI.h"
#include "Editor/widgets/cards/VSTPluginSession.h"
#include "Editor/widgets/cards/VSTRowDocument.h"
#include "vst/VSTPluginLibrary.h"

class QCheckBox;

namespace Ui {
class VSTPluginFilterGUI;
}

// The legacy VSTPlugin row. It shares its document state (VSTRowDocument)
// and its plugin session (VSTPluginSession) with the card, so it only
// builds its original controls, forwards what the user does and draws what
// those two report.
class VSTPluginFilterGUI : public IFilterGUI
{
	Q_OBJECT

public:
	explicit VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap,
		bool stereoInput = false, const std::optional<VST3BusContract>& busContract = std::nullopt,
		std::vector<std::wstring> inputChannels = {}, std::vector<std::wstring> outputChannels = {});
	~VSTPluginFilterGUI() override;

	void store(QString& command, QString& parameters) override;
	void setChannelFlow(const ChannelFlowAtLine& flow) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void on_openPanelButton_clicked();
	void on_pathLineEdit_editingFinished();
	void on_selectButton_clicked();
	void on_embedAction_toggled(bool checked);
	void stereoInputToggled(bool checked);
	void busLayoutPicked();
	void fillToggleClicked(bool checked);
	void pluginStateChanged();
	void showStatus();

private:
	void updatePermissionWarning();
	void updateBusControls();
	void updateFillRows();
	void rebuildFillRow(bool output);

	std::unique_ptr<Ui::VSTPluginFilterGUI> ui;
	// The legacy "StereoInput 1" option stays a flag of this row, outside the
	// document, so a line that carries it is written back unchanged (the card
	// migrates it into the equivalent contract instead).
	bool stereoInput = false;
	// The bus contract and the per-slot channel fill, edited by the two bus
	// dropdowns and the plain combo rows below them.
	VSTRowDocument document;
	std::unique_ptr<VSTPluginSession> session;
	bool fillCollapsed = false;
	bool fillCollapsedFromPrefs = false;
	QWidget* inputFillRow = nullptr;
	QWidget* outputFillRow = nullptr;
	QCheckBox* fillToggle = nullptr;
	QAction* stereoInputAction = nullptr;
};
