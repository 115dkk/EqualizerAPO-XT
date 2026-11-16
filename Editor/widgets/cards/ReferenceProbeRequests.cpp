/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ReferenceProbeRequests.h"

bool ReferenceProbeRequests::begin(const QString& key)
{
	current = key;
	hasCurrent = true;
	if (running.contains(key))
		return false;
	running.insert(key);
	return true;
}

void ReferenceProbeRequests::clear()
{
	current.clear();
	hasCurrent = false;
}

bool ReferenceProbeRequests::finish(const QString& key)
{
	// A key that is not running was answered already (or never asked): its
	// one delivery has happened.
	if (!running.remove(key))
		return false;
	return hasCurrent && key == current;
}
