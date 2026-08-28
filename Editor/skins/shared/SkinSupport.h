/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include "Editor/skins/ISkin.h"

// Common derived tokens shared by every skin.
inline void finishTokens(SkinTokens& t)
{
	t.surfaceRaised = t.cardHover;
	t.surfaceSunken = t.graph;
	t.graphGridMajor = t.border;
	if (t.graphGridMinor.isEmpty())
		t.graphGridMinor = t.border;
	t.focusRing = t.accent;
}

// Factory accessors for the built-in skins. Each is defined in its own .cpp
// and returns a process-lifetime instance; Skins::all() lists them in order.
ISkin* studioSkin();
ISkin* minimalSkin();
ISkin* softSkin();
ISkin* rackSkin();
ISkin* matrixSkin();
