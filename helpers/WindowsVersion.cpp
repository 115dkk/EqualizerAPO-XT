/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See WindowsVersion.h. Moved out of RegistryHelper unchanged apart from the
	cache becoming a function-local static, which also makes the first call
	thread-safe; the old file-scope DWORD was written without synchronisation and
	is reached from the Editor's GUI thread and the device threads alike.
*/

#include "stdafx.h"

#include "WindowsVersion.h"

#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
DWORD productVersion()
{
	// C++11 guarantees the initialisation runs once even under concurrent first
	// calls, which the previous plain-DWORD cache did not.
	static const DWORD cached = []() -> DWORD {
		DWORD handle;
		DWORD size = GetFileVersionInfoSizeW(L"kernel32.dll", &handle);
		if (size == 0)
			return 0;

		std::vector<char> data(size);
		if (!GetFileVersionInfoW(L"kernel32.dll", handle, size, data.data()))
			return 0;

		VS_FIXEDFILEINFO* info;
		UINT length;
		if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length))
			return 0;

		return info->dwProductVersionMS;
	}();

	return cached;
}
}

namespace WindowsVersion
{

bool isAtLeast(unsigned major, unsigned minor)
{
	// The version resource packs each decimal digit into its own nibble, so 6.3
	// is 0x00060003. This only works for major and minor up to 99.
	const DWORD compareVersion = ((major / 10) << 20) + ((major % 10) << 16)
		+ ((minor / 10) << 4) + (minor % 10);

	return productVersion() >= compareVersion;
}

} // namespace WindowsVersion
