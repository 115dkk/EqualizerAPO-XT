/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/ipc/EngineBeacon.h"
#include "services/windows/AudioEngineRestart.h"
#include "services/windows/AudioServiceNames.h"
#include "services/windows/WindowsService.h"
#include "../TestHarness.h"
#include "version.h"
#include <aclapi.h>
#include <sddl.h>

namespace
{
namespace beacon = eapo::ipc::beacon;

std::wstring mappingName(const wchar_t* suffix)
{
	return L"Local\\EAPO.EngineBeacon.Tests." + std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
}

TEST_CASE(EngineOrchestrationTests, testEngineBeaconWireFormat)
{
	const beacon::EngineVersion version{2, 64, 9};
	beacon::Region region{};
	beacon::encode(region, version, 1234);
	harness.expectTrue(beacon::decode(&region, sizeof(region)) == version, "beacon round trip preserves all version fields");
	harness.expectEqual(region.processId, uint32_t(1234), "beacon identifies its publisher");
	harness.expectEqual(region.magic, uint32_t(0x42504145), "wire magic is EAPB in little endian");
	harness.expectEqual(region.layout, uint32_t(1), "wire layout is version one");
	harness.expectEqual(sizeof(region), size_t(24), "wire layout has six fixed-width words");
	harness.expectTrue(beacon::builtVersion() == beacon::EngineVersion{MAJOR, MINOR, REVISION}, "built version comes from version.h");
	for (size_t size = 0; size < sizeof(region); ++size)
		harness.expectTrue(!beacon::decode(&region, size), "short buffers are rejected");
	harness.expectTrue(!beacon::decode(nullptr, sizeof(region)), "null data is rejected");
	std::array<unsigned char, sizeof(region) + 1> unaligned{};
	std::memcpy(unaligned.data() + 1, &region, sizeof(region));
	harness.expectTrue(beacon::decode(unaligned.data() + 1, sizeof(region)) == version, "decoding does not require aligned input");
	region.magic = 0;
	harness.expectTrue(!beacon::decode(&region, sizeof(region)), "wrong magic is rejected");
	beacon::encode(region, version, 1234);
	region.layout = 2;
	harness.expectTrue(!beacon::decode(&region, sizeof(region)), "unknown layouts are rejected");
}

TEST_CASE(EngineOrchestrationTests, testEngineBeaconClassification)
{
	const beacon::EngineVersion installed{2, 64, 0};
	harness.expectTrue(beacon::classify(std::nullopt, installed) == beacon::EngineState::NotLoaded, "missing beacon means not loaded");
	harness.expectTrue(beacon::classify(installed, installed) == beacon::EngineState::Current, "identical versions are current");
	harness.expectTrue(beacon::classify(beacon::EngineVersion{2, 63, 9}, installed) == beacon::EngineState::Stale, "older versions are stale");
	harness.expectTrue(beacon::classify(beacon::EngineVersion{3, 0, 0}, installed) == beacon::EngineState::Stale, "newer versions also require a restart");
}

TEST_CASE(EngineOrchestrationTests, testEngineBeaconPublicationAndPermissions)
{
	const std::wstring name = mappingName(L"roundtrip");
	const beacon::EngineVersion version{2, 64, 1};
	harness.expectTrue(!beacon::readPublished(name.c_str()), "no publication exists initially");
	{
		beacon::Publication publication(version, name.c_str());
		harness.require(publication.published(), "local test beacon publishes");
		harness.expectTrue(beacon::readPublished(name.c_str()) == version, "read-only open decodes published version");
		winutil::UniqueHandle mapping(OpenFileMappingW(READ_CONTROL | FILE_MAP_READ, FALSE, name.c_str()));
		harness.require(static_cast<bool>(mapping), "owner can inspect the mapping descriptor");
		winutil::UniqueLocalPtr<void> descriptor;
		PACL dacl = nullptr;
		harness.require(GetSecurityInfo(mapping.get(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION,
			nullptr, nullptr, &dacl, nullptr, descriptor.put()) == ERROR_SUCCESS, "kernel mapping DACL is readable");
		winutil::UniqueLocalPtr<wchar_t> text;
		harness.require(ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor.get(), SDDL_REVISION_1,
			DACL_SECURITY_INFORMATION, text.put(), nullptr) != FALSE, "descriptor converts back to SDDL");
		harness.expectTrue(std::wstring(text.get()).find(L";;;AU)") != std::wstring::npos, "authenticated users have an explicit ACE");
		harness.require(dacl != nullptr, "mapping does not use a null DACL");
		harness.expectEqual(dacl->AceCount, WORD(4), "only SYSTEM, LOCAL SERVICE, administrators and authenticated users are granted access");
		bool foundReadOnly = false;
		unsigned fullAccessCount = 0;
		for (DWORD i = 0; i < dacl->AceCount; ++i)
		{
			void* entry = nullptr;
			harness.require(GetAce(dacl, i, &entry) != FALSE, "ACE is readable");
			const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
			harness.require(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "all grants are ordinary allow ACEs");
			PSID sid = const_cast<DWORD*>(&ace->SidStart);
			if (IsWellKnownSid(sid, WinAuthenticatedUserSid))
			{
				foundReadOnly = ace->Mask == (SECTION_QUERY | SECTION_MAP_READ);
				harness.expectTrue((ace->Mask & (SECTION_MAP_WRITE | WRITE_DAC | WRITE_OWNER)) == 0,
					"authenticated users cannot write the beacon or change its ACL");
			}
			else if (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinLocalServiceSid)
				|| IsWellKnownSid(sid, WinBuiltinAdministratorsSid))
			{
				if ((ace->Mask & SECTION_ALL_ACCESS) == SECTION_ALL_ACCESS)
					++fullAccessCount;
			}
		}
		harness.expectTrue(foundReadOnly, "authenticated users receive only section query and map read");
		harness.expectEqual(fullAccessCount, 3u, "trusted service accounts and administrators receive full access");
		winutil::UniqueHandle readOnly(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
		harness.require(static_cast<bool>(readOnly), "reader uses only FILE_MAP_READ");
		winutil::UniqueMappedView denied(MapViewOfFile(readOnly.get(), FILE_MAP_WRITE, 0, 0, sizeof(beacon::Region)));
		harness.expectTrue(!denied, "a read-only handle cannot map a writable view even for the owner");
	}
	harness.expectTrue(!beacon::readPublished(name.c_str()), "publication releases mapping and view on destruction");
}

TEST_CASE(EngineOrchestrationTests, testEngineBeaconDoesNotOverwriteForeignMapping)
{
	const std::wstring name = mappingName(L"foreign");
	winutil::UniqueHandle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
		sizeof(beacon::Region), name.c_str()));
	harness.require(static_cast<bool>(mapping), "foreign mapping is created");
	winutil::UniqueMappedView view(MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, sizeof(beacon::Region)));
	harness.require(static_cast<bool>(view), "foreign bytes are writable by their owner");
	std::memset(view.get(), 0x5a, sizeof(beacon::Region));
	beacon::Publication publication({2, 64, 0}, name.c_str());
	harness.expectTrue(!publication.published(), "an existing name is not claimed");
	harness.expectEqual(publication.error(), DWORD(ERROR_ALREADY_EXISTS), "publication preserves the collision error");
	std::array<unsigned char, sizeof(beacon::Region)> expected;
	expected.fill(0x5a);
	harness.expectTrue(std::memcmp(view.get(), expected.data(), expected.size()) == 0, "foreign bytes remain untouched");
}

TEST_CASE(EngineOrchestrationTests, testEngineBeaconPublishesOnlyOnce)
{
	const std::wstring first = mappingName(L"once");
	const std::wstring second = mappingName(L"ignored");
	harness.expectTrue(beacon::publishOnce({2, 64, 0}, first.c_str()), "first publication succeeds");
	harness.expectTrue(beacon::publishOnce({9, 8, 7}, second.c_str()), "subsequent publication returns the first result");
	harness.expectTrue(beacon::readPublished(first.c_str()) == beacon::EngineVersion{2, 64, 0}, "published version is immutable");
	harness.expectTrue(!beacon::readPublished(second.c_str()), "subsequent publication creates no other mapping");
}

TEST_CASE(EngineOrchestrationTests, testAudioEngineRestartServiceOrder)
{
	std::vector<std::wstring> calls;
	std::wstring error;
	const bool result = audioengine::restartAudioEngine([&](const std::wstring& name, unsigned timeout) {
		calls.push_back(L"restart " + name);
		harness.expectEqual(timeout, 30000u, "restart has a bounded service wait");
	}, [&](const std::wstring& name, unsigned timeout) {
		calls.push_back(L"start " + name);
		harness.expectEqual(timeout, 30000u, "start has a bounded service wait");
	}, [&](const std::wstring& message) { error = message; });
	harness.expectTrue(result && error.empty(), "successful restart reports no error");
	harness.expectTrue(calls == std::vector<std::wstring>{L"restart " + std::wstring(audioEndpointBuilderServiceName),
		L"start " + std::wstring(audioServiceName)}, "restart recycles endpoint builder before ensuring AudioSrv runs");
}

TEST_CASE(EngineOrchestrationTests, testAudioEngineRestartReportsFailure)
{
	bool startCalled = false;
	std::wstring error;
	harness.expectTrue(!audioengine::restartAudioEngine([](const std::wstring&, unsigned) {
		throw WindowsServiceError(L"test service failure");
	}, [&](const std::wstring&, unsigned) { startCalled = true; },
		[&](const std::wstring& message) { error = message; }), "throwing restart returns false");
	harness.expectTrue(!startCalled && error == L"test service failure", "restart error is reported without continuing");
	harness.expectTrue(!audioengine::restartAudioEngine([](const std::wstring&, unsigned) {},
		[](const std::wstring&, unsigned) { throw WindowsServiceError(L"test start failure"); },
		[&](const std::wstring& message) { error = message; }), "throwing start returns false");
	harness.expectTrue(error == L"test start failure", "start failure is reported");
	harness.expectTrue(!audioengine::restartAudioEngine([](const std::wstring&, unsigned) { throw std::runtime_error("test"); },
		[](const std::wstring&, unsigned) {}, [](const std::wstring&) { throw std::runtime_error("report"); }),
		"neither unexpected exceptions nor a throwing error reporter escape");
}

TEST_CASE(EngineOrchestrationTests, testAudioEngineRestartRequestOutcomes)
{
	using audioengine::RestartRequest;
	unsigned waits = 0;
	std::optional<unsigned long> childExit = 0;
	const audioengine::WaitForExit wait = [&](HANDLE process, unsigned long timeout) {
		++waits;
		harness.expectTrue(process != nullptr, "wait receives the owned process handle");
		harness.expectEqual(timeout, 123ul, "request forwards the timeout");
		return childExit;
	};
	const audioengine::ElevatedRun run = [&](const std::wstring& exe, const std::wstring& args) {
		harness.expectTrue(exe == L"test Editor.exe" && args == audioengine::kRestartArgument, "request launches only the restart argument");
		winutil::ElevatedLaunch result;
		result.started = true;
		// No child or UAC prompt: the injected waiter treats this owned event as a process handle.
		result.process.reset(CreateEventW(nullptr, TRUE, TRUE, nullptr));
		return result;
	};
	harness.expectTrue(audioengine::requestRestart(L"test Editor.exe", run, wait, 123) == RestartRequest::Restarted, "exit zero means restarted");
	childExit = 3;
	harness.expectTrue(audioengine::requestRestart(L"test Editor.exe", run, wait, 123) == RestartRequest::Failed, "nonzero exit fails");
	childExit.reset();
	harness.expectTrue(audioengine::requestRestart(L"test Editor.exe", run, wait, 123) == RestartRequest::Failed, "timeout or exit-query failure fails");
	for (DWORD error : {DWORD(ERROR_CANCELLED), DWORD(ERROR_ACCESS_DENIED)})
	{
		const auto result = audioengine::requestRestart(L"test Editor.exe", [error](const std::wstring&, const std::wstring&) {
			winutil::ElevatedLaunch launch;
			launch.error = error;
			return launch;
		}, wait, 123);
		harness.expectTrue(result == (error == ERROR_CANCELLED ? RestartRequest::Declined : RestartRequest::Failed), "launch failures distinguish declined approval");
	}
	harness.expectEqual(waits, 3u, "failed launches are never waited on");
	harness.expectTrue(audioengine::requestRestart(L"test Editor.exe", [](const std::wstring&, const std::wstring&) {
		winutil::ElevatedLaunch result;
		result.started = true;
		return result;
	}, wait, 123) == RestartRequest::Failed, "missing process handle is a failure");
}
}
