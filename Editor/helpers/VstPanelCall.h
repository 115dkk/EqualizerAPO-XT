/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "platform/windows/Win32Resource.h"

// Widget-free so the same boundary used by both panel hosts is testable.
template<class Call>
bool vstPanelCallGuarded(const Call& call)
{
	// A plug-in can raise an AV while restoring state or opening its editor.
	// testGuardedPanelFaults uses TestVst2Plugin's state/editor fault modes;
	// both dialog and embedded callers must contain it (audit #467 TD-20).
	__try
	{
		return call();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
