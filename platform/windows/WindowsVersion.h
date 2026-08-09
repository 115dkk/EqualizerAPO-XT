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

// What kernel32.dll's version resource reports. Read from there rather than from
// GetVersionEx, because GetVersionEx has been subject to application-compatibility
// shimming since Windows 8.1 and lies to a process without the right manifest -
// which is the exact version boundary the callers care about.
struct Version
{
	unsigned major = 0;
	unsigned minor = 0;
	// 26100 and up is Windows 11 24H2, where the OS started creating its own
	// subkeys below an endpoint's FxProperties (issue #189).
	unsigned build = 0;
};

Version current();

// True when the running kernel is at least this major.minor.
bool isAtLeast(unsigned major, unsigned minor);

} // namespace WindowsVersion
