/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "Editor/skins/ISkin.h"

// Common derived tokens shared by every skin. surfaceRaised, surfaceSunken,
// graphGridMajor and focusRing are always derived here, so a skin table does
// not set them: the major graph grid is the border colour in every skin
// (minimal's own grid-major values never reached the screen and were removed,
// audit #348 TD-60).
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
