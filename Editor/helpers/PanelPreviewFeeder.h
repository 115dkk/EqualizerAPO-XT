/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)

	The Qt owner of PanelFeedEngine. It maps the environment kill switches,
	starts the feed before the editor session and drives tick() from a GUI-thread
	timer. PanelFeedEngine documents the audio behavior and threading contract.
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
