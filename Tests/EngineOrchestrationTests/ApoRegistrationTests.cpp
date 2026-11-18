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

#include <algorithm>
#include <string>
#include <stdexcept>
#include "services/registry/RegistryPaths.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include "services/security/AudioEngineAccess.h"

#include "asio/AsioRegistration.h"
#include "services/install/ApoRegistration.h"
#include "services/install/InstallWaitBudget.h"
#include "services/windows/WindowsService.h"
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

TEST_CASE(EngineOrchestrationTests, testConfigGrantSkipsAlreadyGrantedFolder)
{
	const auto dir = testDirectory() + L"\\already-granted";
	winutil::UniqueLocalPtr<void> descriptor;
	harness.require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"D:P(A;OICI;FA;;;OW)(A;OICI;0x1301bf;;;BU)(A;OICI;0x1301bf;;;LS)",
		SDDL_REVISION_1, descriptor.put(), nullptr) != FALSE, "temp grant descriptor");
	SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor.get(), FALSE};
	harness.require(CreateDirectoryW(dir.c_str(), &attributes) != FALSE, "create granted temp folder");
	int calls = 0;
	const auto result = AudioEngineAccess::grantConfigAccess(dir, [&](const std::wstring&) {
		++calls;
		return AudioEngineAccess::Grant::Applied;
	});
	harness.expect(result == AudioEngineAccess::Grant::Applied, "existing grants count as applied");
	harness.expectEqual(calls, 0, "already-granted folder does not invoke grant runner");
	RemoveDirectoryW(dir.c_str());
	calls = 0;
	AudioEngineAccess::grantConfigAccess(dir, [&](const std::wstring&) {
		++calls;
		return AudioEngineAccess::Grant::Failed;
	});
	harness.expectEqual(calls, 1, "unqueryable access does not skip the runner");
}

TEST_CASE(EngineOrchestrationTests, testBackupDirectoryHasOnlyAdministrativeWriters)
{
	const auto dir = testDirectory() + L"\\admin-backup";
	{
		auto pinned = WindowsRegistry::openBackupDirectory(dir);
		harness.require(static_cast<bool>(pinned), "create and pin temp backup directory");
		harness.expect((AudioEngineAccess::accessForUsers(dir) & (FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC | WRITE_OWNER)) == 0,
			"backup DACL gives Users no write permission");
	}
	RemoveDirectoryW(dir.c_str());
	harness.expect(WindowsRegistry::deviceBackupDirectory(testDirectory()) ==
		testDirectory() + L"\\EqualizerAPO-XT\\Backups", "backup path uses ProgramData product folder");
}

TEST_CASE(EngineOrchestrationTests, testInstallReportsAclAndServiceFailures)
{
	using Result = ApoRegistration::Result;
	FakeRegistry registry;
	registry.seedKey(audioKey);
	int finishes = 0;
	const auto result = ApoRegistration::finishInstallWithAccess([] { return false; }, [&] {
		++finishes;
		return ApoRegistration::writeAppInstallRegistry(L"C:\\Install", L"", registry);
	});
	harness.expect(result == Result::AclFailed, "failed ACL grant returns AclFailed");
	harness.expectEqual(finishes, 1, "ACL failure still completes install registry role");
	harness.expect(registry.valueExists(appKey, installPathValueName), "ACL failure keeps install registry result");
	harness.expect(ApoRegistration::hookCanComplete(result), "ACL failure is nonfatal to package hook");
	harness.expect(ApoRegistration::finishInstallWithAccess([] { return false; },
		[] { return Result::RegistrationFailed; }) == Result::RegistrationFailed,
		"registration failure takes precedence over ACL warning");
	harness.expect(ApoRegistration::runWithAudioStopped([] { return true; },
		[] { return Result::Success; }, [] { return false; }) == Result::ServiceFailed,
		"failed audio restart returns ServiceFailed");
}

struct FakeAudioEngineServices
{
	std::vector<std::wstring> calls;
	std::vector<std::wstring> errors;
	std::vector<unsigned> timeouts;
	std::vector<std::wstring> dependents{L"AudioSrv"};
	std::vector<std::wstring> stoppedAutoStart;
	bool active = true;
	bool failStop = false;
	bool failSweepQuery = false;
	bool failBeforeQuery = false;
	bool failStart = false;
	std::wstring failStartName;
	bool failRestart = false;
	bool exhaustRestoreBudget = false;

	ApoRegistration::AudioEngineServices operations()
	{
		std::function<std::vector<std::wstring>(const std::wstring&)> sweepQuery;
		if (!stoppedAutoStart.empty() || failSweepQuery)
		{
			sweepQuery = [this](const std::wstring& name) {
				calls.push_back(L"query " + name);
				if (failSweepQuery)
					throw WindowsServiceError(L"Dependent service query failed");
				return stoppedAutoStart;
			};
		}
		return {
			[this](const std::wstring& name, unsigned timeout, bool* wasActive, std::vector<std::wstring>& running) {
				calls.push_back(L"stop " + name);
				timeouts.push_back(timeout);
				if (failBeforeQuery)
					throw WindowsServiceError(L"OpenService access denied");
				*wasActive = active;
				if (active)
					running = dependents;
				if (failStop)
					throw WindowsServiceError(L"Service transition timed out");
			},
			[this](const std::wstring& name, unsigned timeout) {
				calls.push_back(L"start " + name);
				timeouts.push_back(timeout);
				if (failStart || name == failStartName)
					throw WindowsServiceError(L"Service is disabled");
			},
			[this](const std::wstring& name, unsigned timeout) {
				calls.push_back(L"restart " + name);
				timeouts.push_back(timeout);
				if (exhaustRestoreBudget)
					installwait::deadline = GetTickCount64();
				if (failRestart)
					throw WindowsServiceError(L"Service restart timed out");
			},
			[this](const std::wstring& message) { errors.push_back(message); },
			sweepQuery
		};
	}
};

TEST_CASE(EngineOrchestrationTests, testUpdateHookStopsAndStartsAudioEngine)
{
	FakeAudioEngineServices fake;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	const auto result = ApoRegistration::runWithAudioStopped([&] {
		return ApoRegistration::stopAudioEngine(state, services);
	}, [&] {
		fake.calls.push_back(L"install");
		return ApoRegistration::Result::Success;
	}, [&] { return ApoRegistration::startAudioEngine(state, services); });
	harness.expect(result == ApoRegistration::Result::Success, "engine cycle completes update");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder", L"install",
		L"start AudioEndpointBuilder", L"start AudioSrv"},
		"update stops AudioEndpointBuilder and starts it before AudioSrv");
	harness.expect(fake.errors.empty(), "successful engine cycle has no errors");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookRetriesFailedEngineStop)
{
	for (const bool failBeforeQuery : {false, true})
	{
		FakeAudioEngineServices fake;
		fake.failStop = true;
		fake.failBeforeQuery = failBeforeQuery;
		const auto services = fake.operations();
		ApoRegistration::AudioEngineState state;
		const auto result = ApoRegistration::runWithAudioStopped([&] {
			return ApoRegistration::stopAudioEngine(state, services);
		}, [&] {
			fake.calls.push_back(L"install");
			return ApoRegistration::Result::Success;
		}, [&] { return ApoRegistration::startAudioEngine(state, services); });
		harness.expect(result == ApoRegistration::Result::Success, "failed stop does not abort installation");
		harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder", L"install",
			L"restart AudioEndpointBuilder", L"start AudioSrv"},
			"failed stop still installs then restarts AudioEndpointBuilder");
		harness.require(fake.errors.size() == 1, "stop failure reports one error");
		harness.expect(fake.errors.front().find(L"AudioEndpointBuilder") != std::wstring::npos
			&& fake.errors.front().find(failBeforeQuery ? L"access denied" : L"timed out") != std::wstring::npos,
			"stop error includes service name and underlying failure");
	}
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookPreservesInactiveEngine)
{
	FakeAudioEngineServices fake;
	fake.active = false;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	int steps = 0;
	ApoRegistration::runWithAudioStopped([&] { return ApoRegistration::stopAudioEngine(state, services); },
		[&] { ++steps; return ApoRegistration::Result::Success; },
		[&] { return ApoRegistration::startAudioEngine(state, services); });
	harness.expectEqual(steps, 1, "inactive engine does not skip install");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder"},
		"inactive engine is not started without a previous-hook restart debt");
	ApoRegistration::startAudioEngine(state, services);
	harness.expectEqual(fake.calls.size(), size_t(1), "direct start also preserves an inactive engine");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookResumesPreviousExecutableStop)
{
	FakeAudioEngineServices fake;
	fake.active = false;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	state.resumePreviousHook = true;
	harness.expect(ApoRegistration::stopAudioEngine(state, services), "updated hook owes obsolete hook a restart");
	harness.expect(ApoRegistration::startAudioEngine(state, services), "previous hook's stopped engine resumes");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder",
		L"start AudioEndpointBuilder", L"start AudioSrv"}, "updated hook resumes both services");
	FakeAudioEngineServices disabled;
	disabled.failStart = true;
	const auto disabledServices = disabled.operations();
	harness.expectFalse(ApoRegistration::startAudioEngine(state, disabledServices), "disabled service is not enabled by update");
	harness.expectFalse(disabled.errors.empty(), "disabled service start failure is reported");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookRestartsDependentsThePreviousHookStopped)
{
	FakeAudioEngineServices fake;
	fake.active = false;
	fake.stoppedAutoStart = {L"AudioSrv", L"SonarService"};
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	state.resumePreviousHook = true;
	ApoRegistration::stopAudioEngine(state, services);
	const bool started = ApoRegistration::startAudioEngine(state, services);
	harness.expect(started, "previous hook's stopped dependents are a best-effort recovery");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder",
		L"start AudioEndpointBuilder", L"start AudioSrv", L"query AudioEndpointBuilder", L"start SonarService"},
		"updated hook restarts auto-start dependents stopped by the obsolete hook");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookSkipsStoppedDependentSweepForSuccessfulCycle)
{
	FakeAudioEngineServices fake;
	fake.stoppedAutoStart = {L"SonarService"};
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	ApoRegistration::stopAudioEngine(state, services);
	ApoRegistration::startAudioEngine(state, services);
	harness.expect(std::find(fake.calls.begin(), fake.calls.end(), L"query AudioEndpointBuilder") == fake.calls.end(),
		"plain successful engine cycle skips the stopped-dependent sweep");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookStartsCapturedDependentOnceWhenSweepAlsoReturnsIt)
{
	FakeAudioEngineServices fake;
	fake.stoppedAutoStart = {L"AudioSrv", L"SonarService"};
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	state.stopFailed = true;
	state.activeDependents = {L"SonarService", L"AudioSrv"};
	harness.expect(ApoRegistration::startAudioEngine(state, services), "captured dependent recovery succeeds");
	harness.expectEqual(static_cast<unsigned>(std::count(fake.calls.begin(), fake.calls.end(), L"start SonarService")), 1u,
		"a captured dependent returned by the sweep is started once");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookReportsBestEffortSweepFailures)
{
	ApoRegistration::AudioEngineState state;
	state.resumePreviousHook = true;

	FakeAudioEngineServices queryFailure;
	queryFailure.active = false;
	queryFailure.failSweepQuery = true;
	const auto queryServices = queryFailure.operations();
	harness.expect(ApoRegistration::startAudioEngine(state, queryServices), "sweep query failure does not fail the update");
	harness.require(queryFailure.errors.size() == 1, "sweep query failure reports one error");
	harness.expect(queryFailure.errors.front().find(L"AudioEndpointBuilder") != std::wstring::npos
		&& queryFailure.errors.front().find(L"query failed") != std::wstring::npos,
		"sweep query error includes the service and underlying failure");

	FakeAudioEngineServices startFailure;
	startFailure.active = false;
	startFailure.stoppedAutoStart = {L"SonarService"};
	startFailure.failStartName = L"SonarService";
	const auto startServices = startFailure.operations();
	harness.expect(ApoRegistration::startAudioEngine(state, startServices), "sweep start failure does not fail the update");
	harness.require(startFailure.errors.size() == 1, "sweep start failure reports one error");
	harness.expect(startFailure.errors.front().find(L"SonarService") != std::wstring::npos
		&& startFailure.errors.front().find(L"disabled") != std::wstring::npos,
		"sweep start error includes the service and underlying failure");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookRestoresActiveDependents)
{
	FakeAudioEngineServices fake;
	fake.dependents = {L"OtherAudioClient", L"AudioSrv"};
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	ApoRegistration::stopAudioEngine(state, services);
	ApoRegistration::startAudioEngine(state, services);
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder",
		L"start AudioEndpointBuilder", L"start AudioSrv", L"start OtherAudioClient"},
		"active dependents resume in reverse stop order");
	fake.calls.clear();
	fake.dependents.clear();
	state = {};
	ApoRegistration::stopAudioEngine(state, services);
	ApoRegistration::startAudioEngine(state, services);
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder", L"start AudioEndpointBuilder"},
		"inactive AudioSrv is not started for an isolated engine cycle");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookEngineWaitsShareDeadline)
{
	FakeAudioEngineServices fake;
	fake.failStop = true;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	installwait::Budget budget(0);
	ApoRegistration::stopAudioEngine(state, services);
	ApoRegistration::startAudioEngine(state, services);
	harness.expectEqual(fake.timeouts.size(), size_t(3), "stop retry and AudioSrv start attempted even at deadline");
	for (unsigned timeout : fake.timeouts)
		harness.expectEqual(timeout, 0u, "service operations never extend the hook deadline");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookEngineRetrySharesRestoreAllowance)
{
	FakeAudioEngineServices fake;
	fake.failStop = true;
	fake.failRestart = true;
	fake.exhaustRestoreBudget = true;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	const auto result = ApoRegistration::runWithAudioStopped([&] { return ApoRegistration::stopAudioEngine(state, services); },
		[] { return ApoRegistration::Result::Success; },
		[&] { return ApoRegistration::startAudioEngine(state, services); });
	harness.expect(result == ApoRegistration::Result::ServiceFailed, "failed restart is not reported as a successful hook");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder", L"restart AudioEndpointBuilder",
		L"start AudioEndpointBuilder", L"start AudioSrv"}, "failed retry still attempts to restore both services");
	harness.require(fake.timeouts.size() == 4, "all recovery calls recorded");
	harness.expect(fake.timeouts[0] <= 4000 && fake.timeouts[1] <= 4000, "stop and retry each capped at four seconds");
	harness.expectEqual(fake.timeouts[2], 0u, "fallback start cannot renew spent restore allowance");
	harness.expectEqual(fake.timeouts[3], 0u, "AudioSrv cannot renew spent restore allowance");
	harness.expectEqual(fake.errors.size(), size_t(2), "stop and restart failures both reach error callback");
}

TEST_CASE(EngineOrchestrationTests, testUpdateHookEngineRestoresAfterThrow)
{
	FakeAudioEngineServices fake;
	fake.failStop = true;
	const auto services = fake.operations();
	ApoRegistration::AudioEngineState state;
	bool caught = false;
	try
	{
		ApoRegistration::runWithAudioStopped([&] { return ApoRegistration::stopAudioEngine(state, services); },
			[]() -> ApoRegistration::Result { throw std::runtime_error("migration failed"); },
			[&] { return ApoRegistration::startAudioEngine(state, services); });
	}
	catch (const std::runtime_error&)
	{
		caught = true;
	}
	harness.expect(caught, "engine seam preserves install exception");
	harness.expect(fake.calls == std::vector<std::wstring>{L"stop AudioEndpointBuilder",
		L"restart AudioEndpointBuilder", L"start AudioSrv"}, "failed stop and throwing install still recycle the engine once");
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

	ApoRegistration::cleanupAppRegistry(L"C:\\Install\\EqualizerAPO-XT", registry);

	harness.expectFalse(registry.valueExists(audioKey, L"DisableProtectedAudioDG"),
		"the audiodg flag is removed");
	harness.expect(registry.keyExists(appKey),
		"a non-empty app key is preserved (the user's ConfigPath lives there)");

	FakeRegistry emptyCase;
	emptyCase.seedKey(audioKey);
	emptyCase.seedKey(appKey);
	ApoRegistration::cleanupAppRegistry(L"C:\\Install\\EqualizerAPO-XT", emptyCase);
	harness.expectFalse(emptyCase.keyExists(appKey),
		"an empty app key is removed");
}

TEST_CASE(EngineOrchestrationTests, testCleanupForgetsTheInstallTree)
{
	const std::wstring installDir = L"C:\\Install\\EqualizerAPO-XT";
	const std::wstring runKey = eapo::asio::AsioRegistration::autoStartKey();
	const std::wstring runName = eapo::asio::AsioRegistration::autoStartValueName();
	FakeRegistry registry;
	registry.seedKey(audioKey);
	registry.seedString(appKey, L"InstallPath", installDir);
	registry.seedString(appKey, L"ConfigPath", installDir + L"\\config");
	registry.seedString(appKey, L"EnableTrace", L"false");
	registry.seedString(appKey, L"MigratedFrom", L"C:\\Program Files\\EqualizerAPO\\config");
	registry.seedString(appKey, L"MigrationStamp", L"2026-11-05T12:00:00");
	registry.seedDword(appKey, L"MigratedFiles", 3);
	registry.seedString(runKey, runName,
		L"\"C:\\Install\\EqualizerAPO-XT\\EqualizerAPOHost.exe\" --resident");

	ApoRegistration::cleanupAppRegistry(installDir, registry);

	harness.expectFalse(registry.valueExists(appKey, L"InstallPath"),
		"the removed install path is forgotten");
	harness.expectFalse(registry.valueExists(appKey, L"ConfigPath"),
		"a ConfigPath below the removed install tree is forgotten");
	harness.expectFalse(registry.valueExists(appKey, L"MigratedFrom")
		|| registry.valueExists(appKey, L"MigrationStamp")
		|| registry.valueExists(appKey, L"MigratedFiles"),
		"migration breadcrumbs are forgotten");
	harness.expectFalse(registry.valueExists(runKey, runName),
		"an engine host autostart command below the removed install tree is forgotten");
	harness.expect(registry.readValue(appKey, L"EnableTrace") == L"false",
		"EnableTrace survives cleanup");
	harness.expect(registry.keyExists(appKey),
		"the app key remains while a user setting lives there");

	FakeRegistry external;
	external.seedKey(audioKey);
	external.seedString(appKey, L"ConfigPath", L"D:\\My Configs");
	external.seedString(runKey, runName, L"\"C:\\Elsewhere\\EqualizerAPOHost.exe\" --resident");
	ApoRegistration::cleanupAppRegistry(installDir, external);
	harness.expect(external.readValue(appKey, L"ConfigPath") == L"D:\\My Configs",
		"a ConfigPath outside the install tree survives cleanup");
	harness.expect(external.valueExists(runKey, runName),
		"an unrelated engine host autostart command survives cleanup");

	FakeRegistry boundary;
	boundary.seedKey(audioKey);
	boundary.seedString(appKey, L"ConfigPath", L"C:\\Install\\EqualizerAPO-XTX\\config");
	ApoRegistration::cleanupAppRegistry(installDir, boundary);
	harness.expect(boundary.valueExists(appKey, L"ConfigPath"),
		"a sibling directory sharing the install prefix survives cleanup");
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
