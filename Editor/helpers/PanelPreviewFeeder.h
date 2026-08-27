/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)

	The panel preview feed: while a plugin panel is open, capture the system
	mix via WASAPI loopback and run it through the Editor's preview instance,
	so level meters and analyzers inside the plugin UI show live audio. The
	processed output is discarded - the audible signal path stays in the
	audio service, and the Editor keeps talking to it through the
	configuration file only.
*/

#pragma once

#include <memory>

#include <QObject>
#include <QTimer>

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
	struct CaptureState;
	std::unique_ptr<CaptureState> capture;
	QTimer pumpTimer;
};
