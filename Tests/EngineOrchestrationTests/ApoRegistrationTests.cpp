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
	on the machine running the tests). The device sweep they call,
	uninstallAllDeviceApos, takes an injected default-device lookup and is
	driven in DeviceApoInfoTests next to the per-device uninstall semantics;
	the pieces here are the registry role those hooks delegate to.

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
#include "../TestHarness.h"

#include "Tests/FakeRegistry.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
using test::FakeRegistry;

const std::wstring appKey = APP_REGPATH;
const std::wstring audioKey = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";

// A scratch directory for the F034 contract (ConfigPath must exist when the
// registry role reports Success). The folder does not exist yet: the
// registry role has to create it. Callers remove it when done.
std::wstring makeScratchDir()
{
	return testDirectory() + L"\\aporeg-config";
}

void removeScratchDir(const std::wstring& dir)
{
	if (!dir.empty())
		RemoveDirectoryW(dir.c_str());
}

TEST_CASE(EngineOrchestrationTests, testInstallRegistryWritesTheAppVocabulary)
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

TEST_CASE(EngineOrchestrationTests, testInstallRegistryNeverOverwritesUserValues)
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

TEST_CASE(EngineOrchestrationTests, testCleanupRemovesTheFlagButOnlyEmptyKeys)
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

TEST_CASE(EngineOrchestrationTests, testClsidTreeRoundTrip)
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

TEST_CASE(EngineOrchestrationTests, testClsidTreeFailuresReachTheCallerForRollback)
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


TEST_CASE(EngineOrchestrationTests, testMissingMigrationRootGetsOnePackagedDefault)
{
	class CountingRegistry final : public FakeRegistry
	{
	public:
		void writeValue(const std::wstring& key, const std::wstring& name, const std::wstring& value) override
		{
			FakeRegistry::writeValue(key, name, value);
			if (key == appKey && name == L"ConfigPath")
				++configWrites;
		}
		unsigned configWrites = 0;
	};
	const auto directory = testDirectory() + L"\\aporeg-fallback";
	const auto config = directory + L"\\config";
	CountingRegistry registry;
	registry.seedKey(audioKey);
	harness.require(ApoRegistration::writeAppInstallRegistry(directory, L"", registry) == ApoRegistration::Result::Success,
		"install can defer the migration-owned config root");
	harness.expectEqual(registry.configWrites, 0u, "install has not written ConfigPath");
	// Migration returned without publishing a root (missing environment or failed handoff).
	harness.expect(ApoRegistration::writeMissingConfigFallback(directory, registry) == ApoRegistration::Result::Success,
		"an absent migration result gets the packaged default");
	harness.expect(registry.readValue(appKey, L"ConfigPath") == config, "fallback is the install's packaged config directory");
	harness.expect(pathutil::directoryExists(config), "fallback directory exists before success");
	harness.expectEqual(registry.configWrites, 1u, "fallback publishes ConfigPath once");
	ApoRegistration::writeMissingConfigFallback(directory, registry);
	harness.expectEqual(registry.configWrites, 1u, "repeating fallback does not write the value again");

	CountingRegistry existing;
	existing.seedString(appKey, L"ConfigPath", L"D:\\My Configs");
	harness.expect(ApoRegistration::writeMissingConfigFallback(directory, existing) == ApoRegistration::Result::Success,
		"an existing migration or custom result needs no fallback");
	harness.expect(existing.readValue(appKey, L"ConfigPath") == L"D:\\My Configs", "existing root is untouched");
	harness.expectEqual(existing.configWrites, 0u, "existing root is not rewritten");
	existing.denyRead(appKey);
	harness.expect(ApoRegistration::writeMissingConfigFallback(directory, existing) == ApoRegistration::Result::RegistryFailed,
		"an unreadable value is not treated as absent");
	harness.expectEqual(existing.configWrites, 0u, "unreadable root is not overwritten");

	CountingRegistry wrongType;
	wrongType.seedDword(appKey, L"ConfigPath", 7);
	ApoRegistration::writeMissingConfigFallback(directory, wrongType);
	harness.expectEqual(wrongType.readDWORDValue(appKey, L"ConfigPath"), 7ul, "a value of an unreadable type is retained");
	harness.expectEqual(wrongType.configWrites, 0u, "a wrong-type value is not replaced");
	RemoveDirectoryW(config.c_str());
	RemoveDirectoryW(directory.c_str());
}

TEST_CASE(EngineOrchestrationTests, runApoRegistrationTests)
{
	harness.expect(ApoRegistration::shouldGrantInstallAccess(),
		"already-elevated install with no hand-off keeps both legacy grants");
	harness.expect(ApoRegistration::shouldGrantInstallAccess(false),
		"failed or absent preparation keeps both legacy grants");
	harness.expectFalse(ApoRegistration::shouldGrantInstallAccess(true),
		"prepared install skips both elevated recursive grants");
	test::FakeRegistry unchecked;
	unchecked.seedKey(audioKey);
	const auto missing = ApoRegistration::verify(unchecked, false);
	harness.expectFalse(missing.valid(), "verification reports missing protection and registration");
	harness.expectFalse(unchecked.valueExists(audioKey, L"DisableProtectedAudioDG"), "read-only verification does not repair the registry");
	test::FakeRegistry deferred;
	deferred.seedKey(audioKey);
	harness.expect(ApoRegistration::writeAppInstallRegistry(L"C:\\Install", L"", deferred) == ApoRegistration::Result::Success,
		"migration-owned config root can be deferred");
	harness.expectFalse(deferred.valueExists(appKey, L"ConfigPath"),
		"install does not publish a disposable ConfigPath before migration chooses the root");
}
