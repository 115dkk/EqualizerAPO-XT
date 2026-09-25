/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Assembles a standard Windows .vst3 bundle around a module the build staged
	beside the test executable (audit #348 D4/TD-72: Vst3HostTests and
	SubwooferRoutingVst3Tests each carried a copy of this). The host resolves
	a bundle through Contents\<platform>-win\<module>, so the tests exercise
	bundle resolution rather than loading the module file directly.
	Header-only and framework-free like TestHarness.h.
*/

#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace test
{

// The module path inside a bundle for the architecture this test binary was
// built for.
inline std::wstring vst3BundleModulePath(const std::wstring& bundle, const wchar_t* moduleName)
{
	const std::wstring contents = bundle + L"\\Contents";
#if defined(_M_ARM64)
	const std::wstring platform = contents + L"\\arm64-win";
#elif defined(_WIN64)
	const std::wstring platform = contents + L"\\x86_64-win";
#else
	const std::wstring platform = contents + L"\\x86-win";
#endif
	return platform + L"\\" + moduleName;
}

// Copies directory\sourceModule into directory\bundleName as
// Contents\<platform>-win\moduleName and returns the bundle path, or an empty
// string when the staged module is missing or a step fails. A leftover
// "<module>.exit" marker from an earlier run is removed first: the test
// plugin writes it in ExitDll and Vst3HostTests checks that it appears, so a
// stale one would pass that check without an unload.
inline std::wstring prepareVst3Bundle(const std::wstring& directory, const wchar_t* sourceModule,
	const wchar_t* bundleName, const wchar_t* moduleName)
{
	const std::wstring source = directory + L"\\" + sourceModule;
	if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
		return std::wstring();

	auto ensureDirectory = [](const std::wstring& path) {
		return CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
	};
	const std::wstring bundle = directory + L"\\" + bundleName;
	const std::wstring contents = bundle + L"\\Contents";
	const std::wstring module = vst3BundleModulePath(bundle, moduleName);
	const std::wstring platform = module.substr(0, module.find_last_of(L"\\/"));
	if (!ensureDirectory(bundle) || !ensureDirectory(contents) || !ensureDirectory(platform))
		return std::wstring();

	DeleteFileW((module + L".exit").c_str());
	if (CopyFileW(source.c_str(), module.c_str(), FALSE) == FALSE)
		return std::wstring();
	return bundle;
}

} // namespace test
