/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// Kernel version for diagnostics; cached for the lifetime of this process.
#pragma once

namespace WindowsVersion
{

// What kernel32.dll's version resource reports. Read from there rather than from
// GetVersionEx, because GetVersionEx has been subject to application-compatibility
// shimming since Windows 8.1 and lies to a process without the right manifest -
// Diagnostics must report the real version.
struct Version
{
	unsigned major = 0;
	unsigned minor = 0;
	// 26100 and up is Windows 11 24H2, where the OS started creating its own
	// subkeys below an endpoint's FxProperties (issue #189).
	unsigned build = 0;
};

Version current();

} // namespace WindowsVersion
