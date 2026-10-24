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
	host widget the row hands over), what the user asked of the panel while
	the library was still loading (VSTPanelIntent), the idle poll that reads
	the plug-in's state back, and the permission warnings. Both widgets used
	to carry a copy of all of this (audit #348 B1, audit #422 D3); they now
	only build their controls, forward user actions here and draw what this
	reports.

	It owns no display widgets. The load and panel messages both rows show
	use the legacy row's translation context, where they come from (audit
	#422 TD-57); the card's own texts stay with the card.
*/

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include "Editor/helpers/PanelPreviewFeeder.h"
#include "Editor/widgets/cards/VSTPanelIntent.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

class QEvent;
class QWidget;

class VSTPluginSession : public QObject
{
	Q_OBJECT

public:
	// The row this session serves. The legacy row names a missing library
	// in its status text; the card shows it as a state instead.
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
		// The library is being loaded on the loader thread. The text stays empty;
		// the row decides how to present the transient state.
		bool loading = false;
	};

	VSTPluginSession(Row row, std::shared_ptr<VSTPluginLibrary> library, std::wstring chunkData,
		std::unordered_map<std::wstring, float> paramMap);
	~VSTPluginSession() override;

	const std::shared_ptr<VSTPluginLibrary>& library() const;
	// The Editor's own plug-in instance; nullptr until a load succeeds.
	VSTPluginInstance* instance() const;
	const std::wstring& chunkData() const;
	const std::unordered_map<std::wstring, float>& paramMap() const;
	const Status& status() const;
	bool embedded() const;
	bool autoApplyDialog() const;
	void setAutoApplyDialog(bool autoApply);
	bool loading() const;

	// Requests the library load without blocking the UI thread, then creates the
	// instance on the UI thread. loadFinished is emitted for synchronous and
	// asynchronous completion alike.
	void requestPlugin();
	// Requests whose completion callbacks have not yet run on the UI thread.
	static int pendingLoads();
	// Waits for them, handling events meanwhile; false after timeoutMs.
	static bool waitForPendingLoads(int timeoutMs);
	// True when writtenPath (the path as the row shows it) differs from the
	// loaded library's path.
	bool libraryDiffers(const QString& writtenPath) const;
	// Switches to the library writtenPath names, resolved against the
	// default VSTPlugins directory, and loads it. The plug-in state starts
	// over unless the new plug-in has the old one's unique ID; stateChanged
	// follows once the new library's load finished. An embedded panel (or
	// one waiting for its load) closes before the old instance goes and the
	// new plug-in embeds into the same host again.
	void replaceLibrary(const QString& writtenPath);

	// Where the row embeds the panel: host is the widget the panel goes into,
	// showHost shows or hides it together with whatever the row swaps it
	// with. The row calls this once, before its first requestEmbed.
	void setEmbedHost(QWidget* host, std::function<void(bool shown)> showHost);
	// The embed option. On: embeds the panel into the host now, or, with no
	// instance yet, requests the load and embeds once it lands (a failed load
	// drops the option). Off: takes the panel out and drops a waiting embed.
	// embedStateChanged follows. A panel that could not open is reported
	// through the status.
	void requestEmbed(bool enable);
	// An embed waits for the library's load.
	bool embedPending() const;
	// Opens the plug-in's own dialog, modal, over dialogParent, now or once a
	// requested load lands; nothing while the panel is embedded. Returns true
	// when it requested that load (the row then shows its loading state).
	// Emits stateChanged when the dialog is applied or accepted; reports a
	// panel that could not open through the status.
	bool requestPanel(QWidget* dialogParent);

	// The library-readability warning the legacy row shows (the card carries
	// the same verdict on its status line); empty when the audio service can
	// read the library.
	QString libraryPermissionWarning() const;
	// The files the saved plug-in state points to that the audio service
	// cannot read, as warning text; empty when there are none.
	QString chunkPermissionWarning() const;

protected:
	// Follows the embedded panel's window to a display with another scale.
	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	// status() changed.
	void statusChanged();
	// A requested load finished, including a synchronous fast path.
	void loadFinished();
	// The plug-in state changed (dialog applied or accepted, the idle
	// read-back found a change, a replaced library finished loading); the row
	// must store its line again.
	void stateChanged();
	// embedded() or embedPending() changed, or an embed the row asked for
	// cannot happen: the row brings its embed option and panel button in line
	// (the option stays on only while the panel is embedded or waits).
	void embedStateChanged();
	// The plug-in automated a parameter and the new state was read back; the
	// row must store its line again.
	void automated();
	// The embedded panel asks for a new size.
	void sizeRequested(int width, int height);

private slots:
	void applyDialog();
	void onIdle();

private:
	// Loads the library synchronously and creates the instance unless one
	// exists; emits statusChanged. Only for a library that needs no file
	// system work (none selected, or held by this process already).
	void initPlugin();
	void applyLoadResult(int result);
	// Runs after every requested load: the row refreshes, then whatever the
	// user asked for while it loaded happens.
	void finishLoad();
	void finishPendingReplacement();
	void openDialog(QWidget* dialogParent);
	// Embeds the panel into host (which the caller has shown) or takes it
	// out again. Returns embedded(): false when embedding failed, which is
	// reported through the status.
	bool setEmbedded(bool enable, QWidget* host);
	// Shows the host, embeds, and hides the host again when that failed.
	void applyEmbed(bool enable);
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
	unsigned loadGeneration = 0;
	VSTPanelIntent intent;
	QWidget* embedHost = nullptr;
	// The embedded panel's top-level window, watched for scale changes, and
	// the device pixel ratio the panel was last laid out at.
	QPointer<QWidget> scaleWatchWindow;
	double embeddedScale = 1.0;
	std::function<void(bool shown)> showEmbedHost;
	QPointer<QWidget> pendingDialogParent;
	bool replacementPending = false;
	int replacementOldId = 0;
	QElapsedTimer lastReadTimer;
	// Declared after effect on purpose: reverse member destruction stops the
	// pump before the instance it feeds goes away.
	PanelPreviewFeeder previewFeeder;
};
