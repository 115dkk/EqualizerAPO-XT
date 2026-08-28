/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)

	The panel preview feed: while a plugin panel is open, run the Editor's
	preview instance on live audio so meters and analyzers inside the plugin
	UI work - and, when the plugin generates a signal of its own (a
	calibration noise, a test sweep), play that signal out loud. The state
	machine and the WASAPI plumbing live in PanelFeedEngine; this class is
	the Qt face that owns the GUI-thread pump timer and the environment kill
	switches. The audible signal path of the system stays in the audio
	service, and the Editor keeps talking to it through the configuration
	file only.
*/

#pragma once

#include <QObject>
#include <QTimer>

#include "Editor/helpers/PanelFeedEngine.h"

class VSTPluginInstance;

class PanelPreviewFeeder final : public QObject
{
	Q_OBJECT

public:
	PanelPreviewFeeder();
	~PanelPreviewFeeder() override;

	// Call before startEditing(): capturing prepares the instance for the
	// mix format, and VST3 setupProcessing is only legal while the processor
	// is still deactivated. The first pump() can only fire once the event
	// loop runs again, by which time the caller's editor session holds the
	// Processing state.
	void start(VSTPluginInstance* effect);
	void stop();

private slots:
	void pump();

private:
	PanelFeedEngine engine;
	QTimer pumpTimer;
};
