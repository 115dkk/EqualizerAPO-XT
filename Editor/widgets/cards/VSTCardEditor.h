/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Modern card body for VSTPlugin rows. It ports the proven plugin-lifecycle
	logic from the legacy VSTPluginFilterGUI (initialise, open panel, embed,
	store) into a code-built, card-native layout, holding the opaque plugin
	state (chunkData / paramMap) and reproducing it verbatim on store(). A
	mechanical round-trip self-test (--selftest-vst) confirmed this state
	survives parse -> store -> parse without loss.
*/

#pragma once

#include <memory>
#include <unordered_map>

#include <QElapsedTimer>

#include "Editor/IFilterGUI.h"
#include "helpers/VSTPluginLibrary.h"

class QLineEdit;
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
	void pathEditingFinished();
	void selectFile();
	void embedToggled(bool checked);
	void onIdle();

private:
	void initPlugin();
	bool embedPlugin();
	void updatePermissionWarning();
	void onAutomate();
	void onSizeWindow(int w, int h);

	std::shared_ptr<VSTPluginLibrary> library;
	VSTPluginInstance* effect = nullptr;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	bool embedded = false;
	bool autoApplyDialog = false;
	QElapsedTimer lastReadTimer;

	QLineEdit* pathEdit = nullptr;
	QToolButton* selectButton = nullptr;
	QPushButton* openPanelButton = nullptr;
	QToolButton* optionsButton = nullptr;
	QAction* embedAction = nullptr;
	QLabel* statusLabel = nullptr;
	QFrame* frame = nullptr;
	QPlainTextEdit* warningTextEdit = nullptr;
};
