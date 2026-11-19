/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <malloc.h>

#include "platform/windows/Win32Resource.h"
#include "dsp/MxcsrGuard.h"

// Widget-free so the same boundary used by both panel hosts is testable.
template<class Call>
bool vstPanelCallGuarded(const Call& call)
{
	// A plug-in can raise an AV while restoring state or opening its editor.
	// testGuardedPanelFaults uses TestVst2Plugin's state/editor fault modes;
	// both dialog and embedded callers must contain it (audit #467 TD-20).
	const unsigned __int64 fpControlState = captureFpControlState();
	volatile bool stackOverflow = false;
	bool result = false;
	__try
	{
		result = call();
		restoreFpControlState(fpControlState);
	}
	__except ((stackOverflow = GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
		? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)
	{
		restoreFpControlState(fpControlState);
	}
	// A caught overflow consumes the thread's guard page; re-arm it after SEH unwinds.
	if (stackOverflow)
		_resetstkoflw();
	return result;
}
