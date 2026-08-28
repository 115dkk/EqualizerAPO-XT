/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The Start Menu shortcut writer, split out of ApoRegistration (audit #250
	C2): shortcut authoring is shell/COM work with no relation to APO
	registration, and it was a fifth role inside a class named for another
	one. Velopack's vpk pack only emits a shortcut for --mainExe
	(Editor.exe); DeviceSelector is the elevated companion that performs
	per-device APO install/uninstall, so the install hook writes its entry
	into the Public Programs folder here.
*/

#pragma once

#include <string>

namespace StartMenuShortcuts
{
// Writes the DeviceSelector shortcut under the Public Programs folder.
// False on any failure (missing exe, known-folder lookup, COM); the caller
// decides how loudly to complain.
bool create(const std::wstring& installDir);

// Removes the shortcut and, best-effort, its folder when empty.
bool remove();
}
