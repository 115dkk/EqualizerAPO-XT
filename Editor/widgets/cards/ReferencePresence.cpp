/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ReferencePresence.h"

ReferencePresence::Answer ReferencePresence::answerFor(const QString& key) const
{
	return key == knownKey ? known : Answer::Unknown;
}

bool ReferencePresence::begin(const QString& key)
{
	if (key != knownKey)
	{
		knownKey = key;
		known = Answer::Unknown;
	}
	if (known != Answer::Unknown)
		return false;
	return requests.begin(key);
}

bool ReferencePresence::finish(const QString& key, bool exists)
{
	// The request bookkeeping decides whether the answer is still the card's;
	// begin() made the shown key the known key, and forget() drops both.
	if (!requests.finish(key) || key != knownKey)
		return false;
	known = exists ? Answer::Present : Answer::Missing;
	return true;
}

void ReferencePresence::clear()
{
	requests.clear();
}

void ReferencePresence::forget()
{
	requests.clear();
	knownKey.clear();
	known = Answer::Unknown;
}
