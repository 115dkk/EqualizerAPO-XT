/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The Editor's session with one VST plug-in, shared by the modern card
	(VSTCardEditor) and the legacy row (VSTPluginFilterGUI): loading the
	library and reporting why it failed, the preview instance and its
	automation callback, the panel (the plug-in's dialog, or embedded into a
	host widget the caller passes), the idle poll that reads the plug-in's
	state back, and the permission warnings. Both widgets used to carry a
	copy of all of this (audit #348 B1); they now only build their controls,
	forward user actions here and draw what this reports.

	It owns no display widgets. The texts it produces keep the translation
	context of the widget each string came from, so the existing
	translations still apply.
*/

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include "Editor/helpers/PanelPreviewFeeder.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

class QWidget;

class VSTPluginSession : public QObject
{
	Q_OBJECT

public:
	// The row this session serves; it picks the translation context of the
	// texts below.
	enum class Row
	{
		Card,
		Legacy
	};

	// The outcome of the last load attempt or panel opening.
	struct Status
	{
		// The loaded plug-in's name, or why loading or opening failed.
		QString text;
		bool critical = false;
		// No library selected, or the file is not there.
		bool libraryMissing = false;
	};

	VSTPluginSession(Row row, std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData,
		std::unordered_map<std::wstring, float> paramMap);
	~VSTPluginSession() override;

	const std::shared_ptr<VSTPluginLibrary>& library() const;
	// The Editor's own plug-in instance; nullptr until initPlugin succeeds.
	VSTPluginInstance* instance() const;
	const std::wstring& chunkData() const;
	const std::unordered_map<std::wstring, float>& paramMap() const;
	const Status& status() const;
	bool embedded() const;
	bool autoApplyDialog() const;
	void setAutoApplyDialog(bool autoApply);

	// Loads the library and creates the instance unless one exists; emits
	// statusChanged after every attempt.
	void initPlugin();
	// True when writtenPath (the path as the row shows it) differs from the
	// loaded library's path.
	bool libraryDiffers(const QString& writtenPath) const;
	// Switches to the library writtenPath names, resolved against the
	// default VSTPlugins directory, and loads it. The plug-in state starts
	// over unless the new plug-in has the old one's unique ID. The caller
	// closes an embedded panel first.
	void replaceLibrary(const QString& writtenPath);
	// Opens the plug-in's own dialog, modal, over dialogParent. Requires a
	// loaded instance (initPlugin). Emits stateChanged when the dialog is
	// applied or accepted; reports a panel that could not open through the
	// status.
	void openDialog(QWidget* dialogParent);
	// Embeds the panel into host (which the caller has shown) or takes it
	// out again. Returns embedded(): false when embedding failed, which is
	// reported through the status.
	bool setEmbedded(bool enable, QWidget* host);

	// The library-readability warning the legacy row shows (the card carries
	// the same verdict on its status line); empty when the audio service can
	// read the library.
	QString libraryPermissionWarning() const;
	// The files the saved plug-in state points to that the audio service
	// cannot read, as warning text; empty when there are none.
	QString chunkPermissionWarning() const;

signals:
	// status() changed.
	void statusChanged();
	// The plug-in state changed (dialog applied or accepted, the idle
	// read-back found a change); the row must store its line again.
	void stateChanged();
	// The plug-in automated a parameter and the new state was read back; the
	// row must store its line again.
	void automated();
	// The embedded panel asks for a new size.
	void sizeRequested(int width, int height);

private slots:
	void applyDialog();
	void onIdle();

private:
	void onAutomate();
	bool embedPlugin(QWidget* host);
	void acquirePanelProcessing();
	void releasePanelProcessing();
	void reportPanelCrash();

	Row row;
	std::shared_ptr<VSTPluginLibrary> pluginLibrary;
	std::unique_ptr<VSTPluginInstance> effect;
	std::wstring currentChunkData;
	std::unordered_map<std::wstring, float> currentParamMap;
	Status currentStatus;
	bool isEmbedded = false;
	bool ownsPanelProcessing = false;
	bool autoApply = true;
	QElapsedTimer lastReadTimer;
	// Declared after effect on purpose: reverse member destruction stops the
	// pump before the instance it feeds goes away.
	PanelPreviewFeeder previewFeeder;
};
