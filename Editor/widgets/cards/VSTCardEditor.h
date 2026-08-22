/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Modern card body for VSTPlugin rows. It ports the plugin-lifecycle
	logic from the legacy VSTPluginFilterGUI (initialise, open panel, embed,
	store) into a card-native layout, holding the opaque plugin state and options
	(chunkData / paramMap / bus contract) and reproducing them on store(). The
	--selftest-vst round-trip test pins that this state survives
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

#include <QElapsedTimer>

#include "Editor/IFilterGUI.h"
#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/widgets/cards/VSTBusModel.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

class QToolButton;
class QPushButton;
class QFrame;
class QPlainTextEdit;
class QAction;
class FileReferenceController;
class FilterTable;
class VSTBusStrip;

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
	~VSTCardEditor();

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void openPanel();
	void panelButtonClicked();
	void applyDialog();
	void autoApplyToggled(bool checked);
	void pathCommitted(const QString& text);
	void selectFile();
	void importToConfig();
	void embedToggled(bool checked);
	void busLayoutsPicked(VST3BusLayout input, VST3BusLayout output);
	void removeBusLayouts();
	void onIdle();

private:
	void initPlugin();
	bool embedPlugin();
	void updateReferenceState();
	void updateBusControls();
	void updatePermissionWarning();
	void onAutomate();
	void onSizeWindow(int w, int h);

	std::shared_ptr<VSTPluginLibrary> library;
	std::unique_ptr<VSTPluginInstance> effect;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool embedded = false;
	bool autoApplyDialog = false;
	VSTBusModel busModel;
	// Per-slot channel fill for the forced layouts. The card has no editor
	// for these yet; it preserves them losslessly and only drops a side's
	// list when that side's layout changes, because the slot count no longer
	// matches.
	std::vector<std::wstring> inputChannels;
	std::vector<std::wstring> outputChannels;
	// The active device's channel names, for Auto-direction negotiation
	// hints; empty in contexts without a filter table (tests, previews).
	std::vector<std::wstring> deviceChannelNames;
	// Long-form bus message for the reference card's status line (the strip
	// itself only carries the compact verdict). Composed by
	// updateBusControls, consumed by updateReferenceState.
	QString busStatusText;
	ReferenceCardState::Severity busStatusSeverity = ReferenceCardState::Severity::None;
	QElapsedTimer lastReadTimer;

	FileReferenceController* reference = nullptr;
	// The filter table owning this row; nullptr in contexts without one
	// (tests, previews), which merely hides the import affordance.
	FilterTable* filterTable = nullptr;
	QString initErrorText;
	bool libraryMissing = false;

	ReferenceCardView* view = nullptr;
	QToolButton* selectButton = nullptr;
	QToolButton* importButton = nullptr;
	QPushButton* openPanelButton = nullptr;
	QToolButton* optionsButton = nullptr;
	QAction* embedAction = nullptr;
	QAction* removeBusAction = nullptr;
	VSTBusStrip* busStrip = nullptr;
	QFrame* frame = nullptr;
	QPlainTextEdit* warningTextEdit = nullptr;
};
