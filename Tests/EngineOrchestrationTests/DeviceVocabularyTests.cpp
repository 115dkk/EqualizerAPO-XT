/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The device-side vocabulary shared by the installer, the Device Selector
	and the APO: the registry value names an install owns, the device test
	pipe name, install-state comparison, the Voicemeeter strips, and the
	process search behind the Voicemeeter client check.
*/

#include <algorithm>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceAPOInfoKeys.h"
#include "devices/VoicemeeterAPOInfo.h"
#include "devices/VoicemeeterDetection.h"
#include "platform/windows/ProcessCommandLine.h"
#include "platform/windows/Win32Resource.h"
#include "Tests/FakeRegistry.h"

#include "EngineOrchestrationTestSupport.h"

void testDeviceApoRegistryVocabulary(test::Harness& harness)
{
	harness.expectEqual(allGuidValueNameCount, 5u,
		"the five legacy APO slots stay in their indexed order");
	const auto ownedBegin = std::begin(ownedFxValueNames);
	const auto ownedEnd = std::end(ownedFxValueNames);
	for (const wchar_t* valueName : allGuidValueNames)
		harness.expect(std::find(ownedBegin, ownedEnd, valueName) != ownedEnd,
			"every installed APO GUID value is in the uninstall ownership table");
	for (const wchar_t* valueName : {
		sfxProcessingModesValueName, mfxProcessingModesValueName,
		efxProcessingModesValueName, fxTitleValueName })
	{
		harness.expect(std::find(ownedBegin, ownedEnd, valueName) != ownedEnd,
			"every installed processing value is in the uninstall ownership table");
	}
}

void testDeviceTestPipeNameAllowList(test::Harness& harness)
{
	// The literal DeviceTestThread writes must survive untouched, or the
	// device test silently never gets an answer from the APO.
	harness.expectTrue(sanitizeDeviceTestPipeName(L"EqualizerAPODeviceTest") == L"EqualizerAPODeviceTest",
		"the device test pipe name passes the allow-list unchanged");
	harness.expectTrue(sanitizeDeviceTestPipeName(L"eapo_test-42") == L"eapo_test-42",
		"underscore, dash and digits are pipe-namespace safe");
	// Anything that could leave \\.\pipe\ is dropped whole, not trimmed:
	// a partially accepted name would connect to a pipe nobody listens on.
	for (const wchar_t* hostile : {
		L"..\\..\\PhysicalDrive0", L"a/b", L"a\\b", L"name.", L"..", L"a b",
		L"", L"\u00e9apo" })
	{
		harness.expectTrue(sanitizeDeviceTestPipeName(hostile).empty(),
			"a pipe name with separators, dots, blanks or non-ASCII is ignored");
	}
	harness.expectTrue(sanitizeDeviceTestPipeName(std::wstring(129, L'a')).empty(),
		"an oversized pipe name is ignored");
	harness.expectEqual(sanitizeDeviceTestPipeName(std::wstring(128, L'a')).size(), size_t(128),
		"128 characters is the longest accepted pipe name");
}

void testInstallStateComparisonIgnoresPadding(test::Harness& harness)
{
	using InstallState = DeviceAPOInfo::InstallState;
	alignas(InstallState) unsigned char leftStorage[sizeof(InstallState)];
	alignas(InstallState) unsigned char rightStorage[sizeof(InstallState)];
	std::fill_n(leftStorage, sizeof(leftStorage), static_cast<unsigned char>(0xAA));
	std::fill_n(rightStorage, sizeof(rightStorage), static_cast<unsigned char>(0x55));
	InstallState* left = new (leftStorage) InstallState();
	InstallState* right = new (rightStorage) InstallState();

	harness.expect(!(*left != *right),
		"logically identical install states ignore padding bytes");
	right->allowSilentBufferModification = true;
	harness.expect(*left != *right,
		"a changed install-state field is detected");

	left->~InstallState();
	right->~InstallState();
}

// Audit #275 TD-31: VoicemeeterAPOInfo reads through the injected registry
// port, yet no test file mentioned Voicemeeter at all. The cheapest starting
// point is the edition -> output-count mapping of prependInfos, which decides
// how many Output A* strips the device list offers.
void testVoicemeeterPrependInfosMapsEditionToOutputCount(test::Harness& harness)
{
	struct EditionCase
	{
		const wchar_t* setupExe;
		size_t outputs;
		const char* what;
	};
	const EditionCase editions[] = {
		{ L"VoicemeeterSetup.exe", 1, "standard Voicemeeter offers one output strip" },
		{ L"VoicemeeterProSetup.exe", 3, "Banana offers three output strips" },
		{ L"Voicemeeter8Setup.exe", 5, "Potato offers five output strips" },
	};

	for (const EditionCase& edition : editions)
	{
		test::FakeRegistry registry;
		registry.seedKey(voicemeeterKeyPath);
		registry.seedString(voicemeeterKeyPath, uninstallStringValueName,
			std::wstring(L"C:\\Program Files (x86)\\VB\\Voicemeeter\\") + edition.setupExe);

		std::vector<std::shared_ptr<AbstractAPOInfo>> list;
		VoicemeeterAPOInfo::prependInfos(list, registry);

		harness.requireEqual(list.size(), edition.outputs, std::string(edition.what) + ": strip count");
		harness.expect(list.front()->getConnectionName() == L"Output A1",
			std::string(edition.what) + ": the first strip is Output A1");
		harness.expect(list.back()->getConnectionName()
			== (L"Output A" + std::to_wstring(edition.outputs)),
			std::string(edition.what) + ": the last strip matches the edition");
	}

	// The Wow6432Node fallback answers the same way for a 32-bit install.
	test::FakeRegistry wowRegistry;
	wowRegistry.seedKey(voicemeeterWowKeyPath);
	wowRegistry.seedString(voicemeeterWowKeyPath, uninstallStringValueName,
		L"C:\\VB\\VoicemeeterProSetup.exe");
	std::vector<std::shared_ptr<AbstractAPOInfo>> wowList;
	VoicemeeterAPOInfo::prependInfos(wowList, wowRegistry);
	harness.expectEqual(wowList.size(), size_t(3),
		"the Wow6432Node uninstall key detects the edition too");
}

// Audit #348 E5: the vocabulary the install side and the client now share.
void testVoicemeeterStripVocabulary(test::Harness& harness)
{
	harness.expectEqual(voicemeeterOutputCount(1), 1u, "standard Voicemeeter has one strip");
	harness.expectEqual(voicemeeterOutputCount(2), 3u, "Banana has three");
	harness.expectEqual(voicemeeterOutputCount(3), 5u, "Potato has five");
	harness.expectEqual(voicemeeterOutputCount(0), 1u, "an unknown type is treated as the standard edition");
	harness.expect(voicemeeterOutputName(0) == L"Output A1", "strips are named from A1");
	harness.expect(voicemeeterOutputName(4) == L"Output A5", "to A5 on Potato");
}

namespace
{
// Whether SeDebugPrivilege is on in this process's token; nullopt when the
// token does not hold it at all.
std::optional<bool> debugPrivilegeEnabled()
{
	winutil::UniqueHandle token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
		return std::nullopt;
	LUID luid;
	if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid))
		return std::nullopt;
	DWORD size = 0;
	GetTokenInformation(token.get(), TokenPrivileges, nullptr, 0, &size);
	std::vector<unsigned char> buffer(size);
	if (size == 0 || !GetTokenInformation(token.get(), TokenPrivileges, buffer.data(), size, &size))
		return std::nullopt;
	const TOKEN_PRIVILEGES* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());
	for (DWORD i = 0; i < privileges->PrivilegeCount; i++)
	{
		const LUID_AND_ATTRIBUTES& entry = privileges->Privileges[i];
		if (entry.Luid.LowPart == luid.LowPart && entry.Luid.HighPart == luid.HighPart)
			return (entry.Attributes & SE_PRIVILEGE_ENABLED) != 0;
	}
	return std::nullopt;
}
}

// Audit #348 TD-51: the Voicemeeter client check enabled SeDebugPrivilege on
// every apply, with or without a client to look at, and never turned it off.
void testProcessSearchLeavesTheTokenAsItWas(test::Harness& harness)
{
	const std::optional<bool> before = debugPrivilegeEnabled();

	const std::vector<winutil::ProcessWithCommandLine> none =
		winutil::findProcessesByExeName(L"EqualizerAPO-XT-no-such-process.exe");
	harness.expect(none.empty(), "no process of an unknown name is found");
	harness.expect(debugPrivilegeEnabled() == before, "and with nothing to inspect the token is not touched");

	wchar_t self[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, self, MAX_PATH);
	const wchar_t* selfName = wcsrchr(self, L'\\') != nullptr ? wcsrchr(self, L'\\') + 1 : self;
	const std::vector<winutil::ProcessWithCommandLine> found = winutil::findProcessesByExeName(selfName);
	bool foundSelf = false;
	for (const winutil::ProcessWithCommandLine& process : found)
		foundSelf = foundSelf || process.processId == GetCurrentProcessId();
	harness.expect(foundSelf, "this test process is found by its own name, in any case");
	harness.expect(debugPrivilegeEnabled() == before, "and after inspecting it the token is back as it was");
}
