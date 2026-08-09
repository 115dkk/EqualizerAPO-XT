/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Modern card body for VSTPlugin rows. It ports the plugin-lifecycle
	logic from the legacy VSTPluginFilterGUI (initialise, open panel, embed,
	store) into a card-native layout, holding the opaque plugin state
	(chunkData / paramMap) and reproducing it verbatim on store(). The
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
#include <unordered_map>

#include <QElapsedTimer>

#include "Editor/IFilterGUI.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

class QToolButton;
class QPushButton;
class QFrame;
class QPlainTextEdit;
class QAction;
class FileReferenceController;
class ReferenceCardView;

class VSTCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	VSTCardEditor(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap, bool stereoInput = false, QWidget* parent = nullptr);
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
	void embedToggled(bool checked);
	void stereoInputToggled(bool checked);
	void onIdle();

private:
	void initPlugin();
	bool embedPlugin();
	void updateReferenceState();
	void updatePermissionWarning();
	void onAutomate();
	void onSizeWindow(int w, int h);

	std::shared_ptr<VSTPluginLibrary> library;
	std::unique_ptr<VSTPluginInstance> effect;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool embedded = false;
	bool autoApplyDialog = false;
	bool stereoInput = false;
	QElapsedTimer lastReadTimer;

	FileReferenceController* reference = nullptr;
	QString initErrorText;
	bool libraryMissing = false;

	ReferenceCardView* view = nullptr;
	QToolButton* selectButton = nullptr;
	QToolButton* editButton = nullptr;
	QPushButton* openPanelButton = nullptr;
	QToolButton* optionsButton = nullptr;
	QAction* embedAction = nullptr;
	QAction* stereoInputAction = nullptr;
	QFrame* frame = nullptr;
	QPlainTextEdit* warningTextEdit = nullptr;
};
