/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <guiddef.h>

// Shared by the direct-load suite and the CI child-effect capture gate (audit #467 TD-26).
inline constexpr GUID kTestChildApoClsid = {0x66aecf92, 0xe5cb, 0x4c6b, {0xba, 0x02, 0xdb, 0xd3, 0x64, 0x9b, 0x88, 0x63}};
inline constexpr wchar_t kTestChildApoClsidText[] = L"{66AECF92-E5CB-4C6B-BA02-DBD3649B8863}";
inline constexpr double kTestChildApoGainDb = -6.0;
