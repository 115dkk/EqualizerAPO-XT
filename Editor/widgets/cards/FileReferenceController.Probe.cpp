/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The part of FileReferenceController that runs on the background probe
// thread. Kept out of FileReferenceController.cpp, which EditorLogicTests
// compiles without BackgroundProbe; the request bookkeeping it relies on is
// ReferenceProbeRequests and ReferencePresence, tested there.

#include "FileReferenceController.h"

#include <QFileInfo>

#include "Editor/helpers/BackgroundProbe.h"

bool FileReferenceController::askPresence(std::function<void()> answered)
{
	if (written.isEmpty() || resolved.isEmpty() || !probesPresenceInBackground())
		return true;

	const QString key = presenceKey();
	if (presence.answerFor(key) != ReferencePresence::Answer::Unknown)
		return true;

	pendingPresence = std::move(answered);
	if (!presence.begin(key))
		return false;

	// Delivered only while this controller exists (BackgroundProbe checks its
	// context on the UI thread), and the controller dies with its card.
	const QString path = resolved;
	BackgroundProbe::runFor(this,
		[path]() { return QFileInfo::exists(path); },
		[this, key](bool exists) {
			if (presence.finish(key, exists) && pendingPresence)
				pendingPresence();
		});
	return false;
}

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
