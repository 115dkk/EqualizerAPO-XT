/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Modern card body for VSTPlugin rows. It ports the proven plugin-lifecycle
	logic from the legacy VSTPluginFilterGUI (initialise, open panel, embed,
	store) into a code-built, card-native layout, holding the opaque plugin
	state (chunkData / paramMap) and reproducing it verbatim on store(). A
	mechanical round-trip self-test (--selftest-vst) confirmed this state
	survives parse -> store -> parse without loss.

	AR2 X-1/X-2: the plugin is presented as a named device, not a DLL path. The
	display name (effGetEffectName) is resolved once at card creation and used as
	the reference card's primary label; the DLL path is demoted to the secondary
	metadata line. Clicking the name opens the plugin panel (the Open panel
	button stays as a secondary affordance).
*/

#pragma once

#include <memory>
#include <unordered_map>

#include <QElapsedTimer>

#include "Editor/IFilterGUI.h"
#include "helpers/VSTPluginLibrary.h"

class ReferenceCard;
class QLabel;
class QToolButton;
class QPushButton;
class QFrame;
class QPlainTextEdit;
class QAction;

class VSTCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	VSTCardEditor(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap, QWidget* parent = nullptr);
	~VSTCardEditor();

	void store(QString& command, QString& parameters) override;
	void loadPreferences(const QVariantMap& prefs) override;
	void storePreferences(QVariantMap& prefs) override;

private slots:
	void openPanel();
	void applyDialog();
	void autoApplyToggled(bool checked);
	void pathChanged(const QString& newPath);
	void locateFile();
	void embedToggled(bool checked);
	void onIdle();

private:
	void initPlugin();
	bool embedPlugin();
	void updatePermissionWarning();
	void onAutomate();
	void onSizeWindow(int w, int h);
	// Recompute the reference card from the current library/effect. status, when
	// non-empty, is an error string shown as the card's critical status line.
	void refreshCard(const QString& status);
	// Relative-or-absolute display path for the current library.
	QString displayPath() const;
	QString currentName() const;

	std::shared_ptr<VSTPluginLibrary> library;
	VSTPluginInstance* effect = nullptr;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool embedded = false;
	bool autoApplyDialog = false;
	bool libraryMissing = false;
	QElapsedTimer lastReadTimer;

	ReferenceCard* card = nullptr;
	QToolButton* locateButton = nullptr;
	QPushButton* openPanelButton = nullptr;
	QToolButton* optionsButton = nullptr;
	QAction* embedAction = nullptr;
	QFrame* frame = nullptr;
	QPlainTextEdit* warningTextEdit = nullptr;
};
