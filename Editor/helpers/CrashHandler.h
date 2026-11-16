/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Field crash diagnostics for the Editor and DeviceSelector. On an unhandled SEH exception (or
	std::terminate) it writes a minidump plus a small text report to
	%LOCALAPPDATA%\EqualizerAPO\logs\crash, including the version and the
	last action breadcrumb, so crashes that only reproduce on other machines
	(see the skin-switch crash reported from a PC-bang demo) come back with an
	analyzable stack instead of an anecdote.
*/

#pragma once

#include <string>

namespace CrashHandler
{
// Install the process-wide handlers. Call once, early in main.
void install(const wchar_t* product = L"Editor");

// Allocation-free snapshot and file-name seams; neither writes a crash artifact.
void copyBreadcrumb(wchar_t (&snapshot)[256]) noexcept;
void formatArtifactName(wchar_t (&name)[128], const wchar_t* product,
	unsigned long pid, unsigned long tick, const wchar_t* extension) noexcept;

// Record a UI or loader-thread action (e.g. "applySkin rack dark=0").
// Writers serialize; the crash handler takes no lock. Keep it short.
void setBreadcrumb(const std::wstring& text);
}
