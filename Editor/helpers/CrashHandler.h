/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Field crash diagnostics for the Editor. On an unhandled SEH exception (or
	std::terminate) it writes a minidump plus a small text report to
	%LOCALAPPDATA%\EqualizerAPO\logs\crash, including the version and the
	last skin breadcrumb, so crashes that only reproduce on other machines
	(see the skin-switch crash reported from a PC-bang demo) come back with an
	analyzable stack instead of an anecdote.
*/

#pragma once

#include <string>

namespace CrashHandler
{
// Install the process-wide handlers. Call once, early in main.
void install();

// Record the most recent noteworthy UI action (e.g. "applySkin rack dark=0").
// Copied into the crash report; keep it short.
void setBreadcrumb(const std::wstring& text);
}
