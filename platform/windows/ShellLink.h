/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winutil
{
	// One IShellLink choreography for every .lnk this program writes
	// (audit #275 C5): the Start Menu shortcut writer and the Voicemeeter
	// startup link used to carry one copy each. Below both consumers
	// (services/shell and devices/) for the same layering reason the COM
	// self-registration helper lives here. Empty workingDir / arguments /
	// description / iconPath skip the respective IShellLink call. The caller
	// is responsible for COM being initialized on this thread.
	HRESULT writeShellLink(const std::wstring& target, const std::wstring& workingDir,
		const std::wstring& arguments, const std::wstring& description,
		const std::wstring& iconPath, int iconIndex, const std::wstring& linkPath);
}
