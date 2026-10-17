/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the user asked of a VST plug-in's panel while its library was still
	loading, and what has to happen once the load lands. VSTPluginSession
	drives it for both VST rows; each row used to carry its own copy of these
	flags and of their completion (audit #422 D3). It has no signals and no
	widgets, so EditorLogicTests pins it.
*/

#pragma once

class VSTPanelIntent
{
public:
	// What the session does once a requested load has finished, in this
	// order.
	struct Completion
	{
		// A replaced library finished loading (or failed to): the row stores
		// its line again.
		bool storeLine = false;
		// The embed the user asked for while the library loaded can happen.
		bool embed = false;
		// That embed cannot happen because the load failed: the row drops its
		// embed option.
		bool dropEmbed = false;
		// The dialog the user asked for while the library loaded can open,
		// unless the panel is embedded by then.
		bool openDialogUnlessEmbedded = false;
	};

	// The embed option was switched on while the library loads.
	void parkEmbed();
	// The embed option was switched off: an embed waiting for the load is
	// dropped.
	void dropEmbed();
	// The panel was asked for while the library loads.
	void parkDialog();
	// The row switched to another library; its line is stored again once the
	// new library's load finishes.
	void replacementStarted();

	bool embedPending() const;
	bool dialogPending() const;
	bool replacementPending() const;

	// Consumes every waiting wish. instanceLoaded: the load produced a
	// plug-in instance.
	Completion loadFinished(bool instanceLoaded);

private:
	bool embedWhenLoaded = false;
	bool openWhenLoaded = false;
	bool storeWhenLoaded = false;
};
