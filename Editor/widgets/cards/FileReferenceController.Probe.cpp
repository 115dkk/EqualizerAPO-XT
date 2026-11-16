/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The part of FileReferenceController that runs on the background probe
// thread. Kept out of FileReferenceController.cpp, which EditorLogicTests
// compiles without BackgroundProbe; the request bookkeeping it relies on is
// ReferenceProbeRequests, tested there.

#include "FileReferenceController.h"

#include "Editor/helpers/BackgroundProbe.h"

bool FileReferenceController::askAudioService(const QString& absolutePath, const QString& configPath,
	std::function<void(const QString& problem)> answer)
{
	if (absolutePath.isEmpty() || !checksAudioService())
	{
		probes.clear();
		answer(QString());
		return true;
	}

	const QString key = absolutePath + QLatin1Char('\n') + configPath;
	pendingAnswer = std::move(answer);
	if (!probes.begin(key))
		return false;

	// The delivery runs only while this controller exists (BackgroundProbe
	// checks its context on the UI thread), and the controller dies with its
	// card, so pendingAnswer never reaches a destroyed card.
	BackgroundProbe::runFor(this,
		[absolutePath, configPath]() { return audioServiceVerdict(absolutePath, configPath); },
		[this, key](AudioServiceVerdict verdict) {
			if (probes.finish(key) && pendingAnswer)
				pendingAnswer(audioServiceProblemText(verdict));
		});
	return false;
}
