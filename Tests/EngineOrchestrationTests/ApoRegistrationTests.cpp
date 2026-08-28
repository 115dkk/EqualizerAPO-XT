/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The install hook's registry role and the APO DLL's COM class tree, under
	the fake registry (audit #250 A3/F002). These were the last
	machine-changing registry writes outside the port: until this split the
	only way to see them run was a live elevated install.

	Deliberately NOT driven here: the full install()/uninstall() hooks
	(they restart services, run icacls and write Public Start Menu entries
	on the machine running the tests) and uninstallAllDeviceApos (its
	default-device lookup goes through COM device enumeration, which
	DeviceApoInfoTests already avoids by loading devices directly). The
	per-device uninstall semantics are covered in DeviceApoInfoTests; the
	pieces here are the registry role those hooks delegate to.

	String comparisons use expect(a == b): the harness's expectEqual
	streams its operands into a narrow ostream, which std::wstring cannot.
*/

#include <string>
#include "services/registry/RegistryPaths.h"

#include <windows.h>

#include "services/install/ApoRegistration.h"
#include "services/registry/ClsidRegistration.h"
#include "platform/windows/WindowsPath.h"
#include "services/registry/WindowsRegistry.h"
#include "Tests/TestHarness.h"

#include "FakeRegistry.h"

namespace
{
using test::FakeRegistry;

const std::wstring appKey = APP_REGPATH;
const std::wstring audioKey = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";

// A scratch directory for the F034 contract (ConfigPath must exist when the
// registry role reports Success). Callers remove it when done.
std::wstring makeScratchDir()
{
	wchar_t tempPath[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0)
		return std::wstring();
	return std::wstring(tempPath) + L"eapo-aporeg-test-"
		+ std::to_wstring(GetCurrentProcessId());
}

void removeScratchDir(const std::wstring& dir)
{
	if (!dir.empty())
		RemoveDirectoryW(dir.c_str());
}

void testInstallRegistryWritesTheAppVocabulary(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(audioKey);

	const std::wstring installDir = L"C:\\Install\\EqualizerAPO-XT";
	const std::wstring configDir = makeScratchDir();
	harness.require(!configDir.empty(), "scratch config dir path resolves");

	const ApoRegistration::Result result = ApoRegistration::writeAppInstallRegistry(
		installDir, configDir, registry);

	harness.expect(result == ApoRegistration::Result::Success,
		"registry role reports success");
	harness.expect(registry.readValue(appKey, L"InstallPath") == installDir,
		"InstallPath records the install directory");
	harness.expect(registry.readValue(appKey, L"ConfigPath") == configDir,
		"ConfigPath defaults to the packaged config dir");
	harness.expect(registry.readValue(appKey, L"EnableTrace") == L"false",
		"EnableTrace defaults off");
	harness.expectEqual(registry.readDWORDValue(audioKey, L"DisableProtectedAudioDG"), 1ul,
		"audiodg protection is disabled for the APO");
	harness.expect(pathutil::directoryExists(configDir),
		"the config directory exists when Success is reported (F034)");

	removeScratchDir(configDir);
}

void testInstallRegistryNeverOverwritesUserValues(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(audioKey);
	registry.seedKey(appKey);
	registry.seedString(appKey, L"ConfigPath", L"D:\\My Configs");
	registry.seedString(appKey, L"EnableTrace", L"true");

	const std::wstring configDir = makeScratchDir();
	harness.require(!configDir.empty(), "scratch config dir path resolves");
	const ApoRegistration::Result result = ApoRegistration::writeAppInstallRegistry(
		L"C:\\Install\\EqualizerAPO-XT", configDir, registry);

	harness.expect(result == ApoRegistration::Result::Success,
		"registry role reports success on reinstall");
	harness.expect(registry.readValue(appKey, L"ConfigPath") == L"D:\\My Configs",
		"a user's ConfigPath survives reinstall");
	harness.expect(registry.readValue(appKey, L"EnableTrace") == L"true",
		"a user's EnableTrace survives reinstall");

	removeScratchDir(configDir);
}

void testCleanupRemovesTheFlagButOnlyEmptyKeys(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(audioKey);
	registry.seedDword(audioKey, L"DisableProtectedAudioDG", 1);
	registry.seedKey(appKey);
	registry.seedString(appKey, L"ConfigPath", L"D:\\My Configs");

	ApoRegistration::cleanupAppRegistry(registry);

	harness.expectFalse(registry.valueExists(audioKey, L"DisableProtectedAudioDG"),
		"the audiodg flag is removed");
	harness.expect(registry.keyExists(appKey),
		"a non-empty app key is preserved (the user's ConfigPath lives there)");

	FakeRegistry emptyCase;
	emptyCase.seedKey(audioKey);
	emptyCase.seedKey(appKey);
	ApoRegistration::cleanupAppRegistry(emptyCase);
	harness.expectFalse(emptyCase.keyExists(appKey),
		"an empty app key is removed");
}

void testClsidTreeRoundTrip(test::Harness& harness)
{
	FakeRegistry registry;
	const std::wstring clsid = L"{11111111-2222-3333-4444-555555555555}";
	const std::wstring classKey =
		L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\" + clsid;

	ClsidRegistration::registerClsidTree(registry, clsid,
		L"EqualizerAPO Post-Mix Class", L"C:\\Install\\EqualizerAPO.dll");

	harness.expect(registry.readValue(classKey, L"") == L"EqualizerAPO Post-Mix Class",
		"the class key carries the display name");
	harness.expect(registry.readValue(classKey + L"\\InprocServer32", L"")
		== L"C:\\Install\\EqualizerAPO.dll",
		"InprocServer32 points at the DLL");
	harness.expect(registry.readValue(classKey + L"\\InprocServer32", L"ThreadingModel")
		== L"Both",
		"the threading model is Both");

	ClsidRegistration::unregisterClsidTree(registry, clsid);
	harness.expectFalse(registry.keyExists(classKey),
		"unregister removes the whole class tree");
}

void testClsidTreeFailuresReachTheCallerForRollback(test::Harness& harness)
{
	// DllRegisterServer's rollback (unregister both APO GUIDs) only runs if
	// a failed write escapes as an exception; a swallowed failure would leave
	// a half-registered class.
	FakeRegistry registry;
	const std::wstring clsid = L"{11111111-2222-3333-4444-555555555555}";
	registry.denyCreateKey(
		L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\" + clsid);

	bool threw = false;
	try
	{
		ClsidRegistration::registerClsidTree(registry, clsid,
			L"EqualizerAPO Post-Mix Class", L"C:\\Install\\EqualizerAPO.dll");
	}
	catch (const RegistryError&)
	{
		threw = true;
	}
	harness.expect(threw, "a refused key creation escapes to the rollback");
}
}

void runApoRegistrationTests(test::Harness& harness)
{
	testInstallRegistryWritesTheAppVocabulary(harness);
	testInstallRegistryNeverOverwritesUserValues(harness);
	testCleanupRemovesTheFlagButOnlyEmptyKeys(harness);
	testClsidTreeRoundTrip(harness);
	testClsidTreeFailuresReachTheCallerForRollback(harness);
}
