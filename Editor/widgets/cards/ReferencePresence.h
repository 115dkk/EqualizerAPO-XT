/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What a card knows about whether its file exists (audit #467 TD-59). The
	question goes to the background probe thread, because a path on a
	disconnected network drive stalls the caller for the network timeout;
	until the answer arrives the card shows its reference as "checking".
	This core keeps, without Qt widgets or threads, which key the card shows
	now, the one answer it has for that key, and whether an answer arriving
	from a probe still belongs to it. Keys are opaque: the caller names what
	it asked about.
*/

#pragma once

#include <QString>

#include "ReferenceProbeRequests.h"

class ReferencePresence
{
public:
	enum class Answer
	{
		Unknown,
		Present,
		Missing
	};

	// What is known about key: Unknown unless the answer kept last is for key.
	Answer answerFor(const QString& key) const;
	// The card now shows key. Another key than before forgets the old answer.
	// True when a probe must start: nothing is known about key and no probe
	// for it is running (a running one answers this request too).
	bool begin(const QString& key);
	// A probe for key answered. True when the answer is kept because key is
	// still what the card shows; false when it is stale and dropped.
	bool finish(const QString& key, bool exists);
	// The card asks nothing for now: an answer still running is dropped. What
	// is known for the current key stays known.
	void clear();
	// The user chose or typed the reference again: forget the answer, so the
	// card asks anew even for the same path (the file may exist now).
	void forget();

private:
	ReferenceProbeRequests requests;
	QString knownKey;
	Answer known = Answer::Unknown;
};
