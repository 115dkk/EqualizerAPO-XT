/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Which Windows this is running on.

	The answer decides real behaviour in three places: which APO slots a device
	can be installed into (LFX/GFX only exist as the sole option before Windows
	8.1), whether the 24H2 subkeys below FxProperties have to be worked around,
	and what Device Selector offers. It lived in RegistryHelper, which reads it
	out of kernel32's version resource - not a registry operation at all, and one
	of the two members that forced every translation unit wanting a registry read
	to inherit the Win32 headers.

	The answer is cached after the first call because it cannot change while the
	process runs.
*/

#pragma once

namespace WindowsVersion
{

// True when the running kernel is at least this major.minor. Both are limited to
// 99, which the packed comparison below relies on and which no Windows version
// has come close to.
//
// Reads kernel32.dll's product version rather than asking GetVersionEx, because
// GetVersionEx has been subject to application-compatibility shimming since
// Windows 8.1 and lies to a process without the right manifest - which is the
// exact version boundary the callers care about.
bool isAtLeast(unsigned major, unsigned minor);

} // namespace WindowsVersion
