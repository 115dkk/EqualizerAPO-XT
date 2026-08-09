/*
	This file is part of EqualizerAPO-XT.

	Tests for the part of the install diagnostics that reads the registry.

	Only that part. The report's other sections ask the running machine about file
	ACLs, the elevation of this process and where its own profile is, and a test
	that asserted on those would be asserting on the machine running it. What can
	be judged is the endpoint scan: given a registry, which devices does it say
	have Equalizer APO attached, and does it survive the shapes a real machine
	produces - a driver-locked FxProperties key, an endpoint with a foreign APO,
	a machine with no MMDevices tree at all.

	That scan is also the one piece that used to exist only in PowerShell, where
	nothing tested it and the property GUIDs were written out a second time.
*/

#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "devices/DeviceAPOInfoKeys.h"
#include "services/diagnostics/InstallDiagnostics.h"
#include "services/registry/RegistryHelper.h"
#include "Tests/TestHarness.h"

#include "FakeRegistry.h"

namespace
{
using test::FakeRegistry;

const std::wstring renderGuid = L"{11111111-2222-3333-4444-555555555555}";
const std::wstring captureGuid = L"{22222222-3333-4444-5555-666666666666}";
const std::wstring foreignGuid = L"{33333333-4444-5555-6666-777777777777}";

bool anyLineContains(const std::vector<std::wstring>& lines, const std::wstring& needle)
{
	for (const std::wstring& line : lines)
	{
		if (line.find(needle) != std::wstring::npos)
			return true;
	}
	return false;
}

void seedEndpoint(FakeRegistry& registry, const wchar_t* root, const std::wstring& deviceGuid,
	const std::wstring& deviceName, const std::wstring& connectionName)
{
	const std::wstring deviceKey = std::wstring(root) + L"\\" + deviceGuid;
	registry.seedDword(deviceKey, L"DeviceState", 1);
	registry.seedString(deviceKey + L"\\Properties", deviceValueName, deviceName);
	registry.seedString(deviceKey + L"\\Properties", connectionValueName, connectionName);
}

void testTheScanNamesEveryEndpointHoldingOurApos(test::Harness& harness)
{
	FakeRegistry registry;
	const std::wstring preMix = RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID);
	const std::wstring postMix = RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID);

	seedEndpoint(registry, renderKeyPath, renderGuid, L"Test Speakers", L"Speakers");
	registry.seedString(renderKeyPath L"\\" + renderGuid + L"\\FxProperties", sfxGuidValueName, preMix);
	registry.seedString(renderKeyPath L"\\" + renderGuid + L"\\FxProperties", mfxGuidValueName, postMix);

	seedEndpoint(registry, captureKeyPath, captureGuid, L"Test Microphone", L"Internal Mic");
	registry.seedString(captureKeyPath L"\\" + captureGuid + L"\\FxProperties", sfxGuidValueName, preMix);

	const std::vector<std::wstring> lines = InstallDiagnostics::attachedEndpoints(registry);

	harness.requireEqual(lines.size(), size_t(2),
		"one line per endpoint that holds one of our CLSIDs, and none for anything else");
	harness.expect(anyLineContains(lines, L"Speakers Test Speakers"),
		"the render endpoint is named the way the user knows it, from the endpoint's own Properties values");
	harness.expect(anyLineContains(lines, L"SFX=pre-mix, MFX=post-mix"),
		"both slots are reported, and which of our two APOs is in each; a device with only one attached is a different problem from one with both");
	harness.expect(anyLineContains(lines, L"capture:"),
		"capture endpoints are scanned too, and marked, because an input device with our APO attached looks identical otherwise");
}

void testTheScanIgnoresEndpointsWithSomebodyElsesApos(test::Harness& harness)
{
	FakeRegistry registry;
	seedEndpoint(registry, renderKeyPath, renderGuid, L"Vendor Speakers", L"Speakers");
	registry.seedString(renderKeyPath L"\\" + renderGuid + L"\\FxProperties", sfxGuidValueName, foreignGuid);

	const std::vector<std::wstring> lines = InstallDiagnostics::attachedEndpoints(registry);

	harness.requireEqual(lines.size(), size_t(1),
		"a machine with no Equalizer APO attached produces exactly one line, which says so");
	harness.expect(anyLineContains(lines, L"no endpoint"),
		"and that line says it plainly rather than leaving the section empty, which would read as a failed scan");
}

void testTheScanKeepsGoingPastAnEndpointItCannotRead(test::Harness& harness)
{
	FakeRegistry registry;
	const std::wstring preMix = RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID);

	// A driver that locks its own FxProperties key is exactly the machine this
	// report gets run on, so the scan must not stop at it.
	seedEndpoint(registry, renderKeyPath, foreignGuid, L"Locked Device", L"Headphones");
	registry.seedString(renderKeyPath L"\\" + foreignGuid + L"\\FxProperties", sfxGuidValueName, preMix);
	registry.denyRead(renderKeyPath L"\\" + foreignGuid + L"\\FxProperties");

	seedEndpoint(registry, renderKeyPath, renderGuid, L"Working Device", L"Speakers");
	registry.seedString(renderKeyPath L"\\" + renderGuid + L"\\FxProperties", sfxGuidValueName, preMix);

	const std::vector<std::wstring> lines = InstallDiagnostics::attachedEndpoints(registry);

	harness.expect(anyLineContains(lines, L"could not be read"),
		"the endpoint that refused the read is reported as such, not silently dropped");
	harness.expect(anyLineContains(lines, L"Working Device"),
		"and the scan continues to the endpoints after it, which is the whole reason it catches per device");
}

void testTheScanSurvivesAMachineWithNoAudioTree(test::Harness& harness)
{
	FakeRegistry registry;

	const std::vector<std::wstring> lines = InstallDiagnostics::attachedEndpoints(registry);

	harness.requireEqual(lines.size(), size_t(1),
		"an empty registry is not an error for a read-only report");
	harness.expect(anyLineContains(lines, L"no endpoint"),
		"a missing MMDevices tree reads the same way as one with nothing attached, which is what it means");
}
} // namespace

void runInstallDiagnosticsTests(test::Harness& harness)
{
	testTheScanNamesEveryEndpointHoldingOurApos(harness);
	testTheScanIgnoresEndpointsWithSomebodyElsesApos(harness);
	testTheScanKeepsGoingPastAnEndpointItCannotRead(harness);
	testTheScanSurvivesAMachineWithNoAudioTree(harness);
}
