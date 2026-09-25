/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Modern card body for VSTPlugin rows. The plugin lifecycle (initialise,
	open panel, embed, state read-back) is VSTPluginSession's and the row's
	document state (bus contract, channel fill) is VSTRowDocument's; both are
	shared with the legacy VSTPluginFilterGUI. This card builds its
	card-native controls around them and reproduces the line on store(). The
	--selftest-vst round-trip test pins that the state survives
	parse -> store -> parse without loss.

	The plugin is presented as a named device, not a file with a path. The
	card renders through the active skin's
	ReferenceCardView - plugin display name first (effGetEffectName), the DLL
	location as secondary metadata, a VST2/VST3 format badge, the broken
	library as a missing-state transition with a Locate recovery entry, and
	the name itself as the open-panel affordance (DAW slot grammar).
*/

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/widgets/cards/VSTPluginSession.h"
#include "Editor/widgets/cards/VSTRowDocument.h"
#include "vst/VSTPluginLibrary.h"

class QToolButton;
class QPushButton;
class QFrame;
class QPlainTextEdit;
class QAction;
class FileReferenceController;
class FilterTable;
class VSTBusStrip;
class VSTSlotFillRail;

class VSTCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	VSTCardEditor(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap, bool stereoInput = false,
		const std::optional<VST3BusContract>& busContract = std::nullopt,
		std::vector<std::wstring> deviceChannelNames = std::vector<std::wstring>(),
		FilterTable* filterTable = nullptr,
		QWidget* parent = nullptr,
		std::vector<std::wstring> inputChannels = {},
		std::vector<std::wstring> outputChannels = {});
	~VSTCardEditor() override;

	void store(QString& command, QString& parameters) override;
	void setChannelFlow(const ChannelFlowAtLine& flow) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void openPanel();
	void panelButtonClicked();
	void pluginStateChanged();
	void pathCommitted(const QString& text);
	void selectFile();
	void importToConfig();
	void embedToggled(bool checked);
	void busLayoutsPicked(VST3BusLayout input, VST3BusLayout output);
	void removeBusLayouts();
	void fillSlotPicked(int slot, const QString& value, bool output);
	void fillLatchToggled();
	void removeChannelFill();

private:
	void updateReferenceState();
	void updateBusControls();
	void updateFillRails();
	void updatePermissionWarning();

	// The bus contract and the per-slot channel fill, edited through the
	// bus strip and the two fill rails, against the selection
	// setChannelFlow delivers.
	VSTRowDocument document;
	std::unique_ptr<VSTPluginSession> session;
	// The fold state of the two rails (only meaningful while both exist).
	// Persisted per row; defaults to collapsed while both sides are still
	// implicit so untouched contract cards keep their height.
	bool fillCollapsed = false;
	bool fillCollapsedFromPrefs = false;
	// The active device's channel names, for Auto-direction negotiation
	// hints; empty in contexts without a filter table (tests, previews).
	std::vector<std::wstring> deviceChannelNames;
	// Long-form bus message for the reference card's status line (the strip
	// itself only carries the compact verdict). Composed by
	// updateBusControls, consumed by updateReferenceState.
	QString busStatusText;
	ReferenceCardState::Severity busStatusSeverity = ReferenceCardState::Severity::None;

	FileReferenceController* reference = nullptr;
	// The filter table owning this row; nullptr in contexts without one
	// (tests, previews), which merely hides the import affordance.
	FilterTable* filterTable = nullptr;

	ReferenceCardView* view = nullptr;
	QToolButton* selectButton = nullptr;
	QToolButton* importButton = nullptr;
	QPushButton* openPanelButton = nullptr;
	QToolButton* optionsButton = nullptr;
	QAction* embedAction = nullptr;
	QAction* removeBusAction = nullptr;
	QAction* removeFillAction = nullptr;
	VSTBusStrip* busStrip = nullptr;
	VSTSlotFillRail* inputRail = nullptr;
	VSTSlotFillRail* outputRail = nullptr;
	QFrame* frame = nullptr;
	QPlainTextEdit* warningTextEdit = nullptr;
};
