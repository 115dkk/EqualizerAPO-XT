/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Which background answer about a card's file still matters (audit #467
	TD-59). A card asks the audio-service question off the UI thread each time
	it shows a file; meanwhile the user may type another path, or the card may
	ask again about the same one. This core decides, without Qt widgets or
	threads, whether a question needs a new probe and whether an answer is
	still the card's current one. Keys are opaque: the caller names what it
	asked about.
*/

#pragma once

#include <QSet>
#include <QString>

class ReferenceProbeRequests
{
public:
	// The card now shows key. True when a probe must start; false when one for
	// the same key is already running, whose answer will serve this request.
	bool begin(const QString& key);
	// The card shows something it asks nothing about: answers still running
	// are no longer anyone's.
	void clear();
	// A probe for key finished. True when its answer is for what the card
	// shows now, false when it is stale and must be dropped.
	bool finish(const QString& key);

private:
	QString current;
	bool hasCurrent = false;
	QSet<QString> running;
};
