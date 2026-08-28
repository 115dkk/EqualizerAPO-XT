/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winutil
{
	std::wstring guidToString(REFGUID guid);
}
