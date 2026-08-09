/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See WindowsVersion.h. Moved out of WindowsRegistry unchanged apart from the
	cache becoming a function-local static, which also makes the first call
	thread-safe; the old file-scope DWORD was written without synchronisation and
	is reached from the Editor's GUI thread and the device threads alike.
*/

#include "stdafx.h"

#include "platform/windows/WindowsVersion.h"

#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
WindowsVersion::Version readVersion()
{
	WindowsVersion::Version version;

	DWORD handle;
	DWORD size = GetFileVersionInfoSizeW(L"kernel32.dll", &handle);
	if (size == 0)
		return version;

	std::vector<char> data(size);
	if (!GetFileVersionInfoW(L"kernel32.dll", handle, size, data.data()))
		return version;

	VS_FIXEDFILEINFO* info;
	UINT length;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length))
		return version;

	// VS_FIXEDFILEINFO keeps the major in the high word and the minor in the low
	// word, and the build in the high word of the *LS* field.
	version.major = HIWORD(info->dwProductVersionMS);
	version.minor = LOWORD(info->dwProductVersionMS);
	version.build = HIWORD(info->dwProductVersionLS);
	return version;
}
}

namespace WindowsVersion
{

Version current()
{
	// C++11 guarantees the initialisation runs once even under concurrent first
	// calls, which the previous plain-DWORD cache did not.
	static const Version cached = readVersion();
	return cached;
}

bool isAtLeast(unsigned major, unsigned minor)
{
	// Compare the fields, not a packed integer. The version this replaced packed
	// each decimal digit into its own nibble, which made 10 come out as 0x10 while
	// kernel32 reports it as 0x0A - so isAtLeast(10, 0) answered false on Windows
	// 10 and 11. Nothing had asked it that yet: the only caller in the tree asks
	// for 6.3, where a per-nibble packing and the real one happen to agree.
	const Version version = current();
	if (version.major != major)
		return version.major > major;
	return version.minor >= minor;
}

} // namespace WindowsVersion
