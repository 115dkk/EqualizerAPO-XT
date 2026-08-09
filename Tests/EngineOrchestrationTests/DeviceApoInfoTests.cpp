/*
	This file is part of EqualizerAPO-XT.

	Unit tests for DeviceAPOInfo::load, install and uninstall, driven through
	the IRegistry port with an in-memory fake. Until the port existed these
	three functions had no unit test at all: every one of them writes to
	HKLM\...\MMDevices\Audio, so running them under test would have rewritten
	the APO chain of a real audio endpoint on the machine doing the testing.

	What these tests are for: they record what the code does today. Most of them
	were written before install() ran inside a RegistryTransaction, so that the
	rework could be judged against them; the last two were added with it and are
	the ones that stage a failure midway and check what is left behind. They are
	not a specification. Where the current behaviour looks questionable it is left
	alone and reported, not asserted - a test that pins a wart makes the wart
	harder to remove.

	Host dependency, deliberately not hidden: load()'s install-mode inference
	asks WindowsVersion::isAtLeast(6, 3), which reads the running
	kernel32 and is not part of the port. The two inference tests therefore
	expect whichever answer the host justifies rather than assuming Windows 8.1
	or newer.
*/

#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Unknwn.h before mmdeviceapi.h, matching services/audio/AudioFormatProbe.cpp: the
// MIDL header needs the COM base types, and WIN32_LEAN_AND_MEAN keeps
// windows.h from supplying them. mmdeviceapi.h is here only for the
// DEVICE_STATE_* bits load() tests, and mmreg.h for the WAVEFORMATEXTENSIBLE
// that goes into the endpoint's format value.
#include <Unknwn.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceAPOInfoKeys.h"
#include "services/registry/RegistryHelper.h"
#include "platform/windows/WindowsVersion.h"
#include "Tests/TestHarness.h"

#include "FakeRegistry.h"

namespace
{
using test::FakeRegistry;

const std::wstring testDeviceGuid = L"{11111111-2222-3333-4444-555555555555}";
// A second, valid GUID handed to load() as the default-device argument. Any
// non-empty value keeps load() from asking the COM device enumerator which
// endpoint is default, which is the one thing in load() the port cannot stand
// in for.
const std::wstring otherDeviceGuid = L"{99999999-8888-7777-6666-555555555555}";
const std::wstring vendorPreMixGuid = L"{AAAAAAAA-1111-2222-3333-444444444444}";
const std::wstring vendorPostMixGuid = L"{BBBBBBBB-1111-2222-3333-444444444444}";

const std::wstring renderDeviceKey = renderKeyPath L"\\" + testDeviceGuid;
const std::wstring propertiesKey = renderDeviceKey + L"\\Properties";
const std::wstring fxPropertiesKey = renderDeviceKey + L"\\FxProperties";
const std::wstring childApoKey = childApoPath L"\\" + testDeviceGuid;

const std::wstring deviceName = L"Test Audio Device";
const std::wstring connectionName = L"Speakers";

// The harness formats diagnostics into a narrow ostringstream, which has no
// insertion operator for wide strings. Every value these tests compare is
// ASCII, so a byte-wise narrowing is enough to keep failures readable.
std::string narrow(const std::wstring& text)
{
	std::string result;
	result.reserve(text.size());
	for (wchar_t character : text)
		result.push_back(character < 128 ? static_cast<char>(character) : '?');
	return result;
}

// The registry shape Windows keeps for one render endpoint: the device key
// with its DeviceState, and the Properties subkey holding the two friendly
// names load() reads. FxProperties is left out on purpose - each test adds the
// FxProperties shape it is about.
void seedRenderDevice(FakeRegistry& registry, unsigned long deviceState = DEVICE_STATE_ACTIVE)
{
	registry.seedDword(renderDeviceKey, L"DeviceState", deviceState);
	registry.seedString(propertiesKey, connectionValueName, connectionName);
	registry.seedString(propertiesKey, deviceValueName, deviceName);
}

std::wstring ourPreMixGuid()
{
	return RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID);
}

std::wstring ourPostMixGuid()
{
	return RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID);
}

void testLoadWithoutFxPropertiesMarksTheDeviceExperimental(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);

	// The endpoint's mix format, stored the way Windows stores it: eight bytes
	// of header the audio stack owns, then the WAVEFORMATEX(TENSIBLE) itself.
	// That offset is the surprising part of the parse, so pin it.
	std::vector<unsigned char> formatBlob(8 + sizeof(WAVEFORMATEXTENSIBLE), 0);
	WAVEFORMATEXTENSIBLE waveFormat = {};
	waveFormat.Format.wFormatTag = static_cast<WORD>(WAVE_FORMAT_EXTENSIBLE);
	waveFormat.Format.nChannels = static_cast<WORD>(6);
	waveFormat.Format.nSamplesPerSec = 48000ul;
	waveFormat.Format.wBitsPerSample = static_cast<WORD>(32);
	waveFormat.Format.cbSize = static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
	waveFormat.dwChannelMask = 0x3Ful; // 5.1
	std::memcpy(formatBlob.data() + 8, &waveFormat, sizeof(waveFormat));
	registry.seedBinary(propertiesKey, formatValueName, formatBlob);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid),
		"a present device has to load even with no FxProperties key, because that is exactly the device the user still needs to be offered");

	harness.expectFalse(info.isInstalled(),
		"nothing of ours can be registered when the key our GUIDs would live in does not exist");
	harness.expect(info.isExperimental(),
		"a missing FxProperties key is what marks a device experimental: installing means creating a key the audio driver never made");
	harness.expectFalse(info.isInput(),
		"the device was seeded below Render, so load has to classify it as an output");
	harness.expectEqual(narrow(info.getDeviceName()), narrow(deviceName),
		"the device name comes from the Properties value the endpoint stores it in");
	harness.expectEqual(narrow(info.getConnectionName()), narrow(connectionName),
		"the connection name comes from its own Properties value, not from the device name");
	harness.expectEqual(info.getChannelCount(), 6u,
		"the channel count is read from the WAVEFORMATEX that starts eight bytes into the format blob");
	harness.expectEqual(info.getSampleRate(), 48000u,
		"the sample rate comes from the same WAVEFORMATEX");
	harness.expectEqual(info.getChannelMask(), 0x3Ful,
		"an extensible format carries the channel mask, so the separate mask value is not consulted");
	harness.expectEqual(static_cast<int>(info.getCurrentInstallState().installMode),
		static_cast<int>(DeviceAPOInfo::INSTALL_LFX_GFX),
		"with no FxProperties key the mode inference never runs, so the LFX/GFX default is what survives");
}

void testLoadSkipsDevicesTheDriverReportsAsNotPresent(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry, DEVICE_STATE_NOTPRESENT);

	DeviceAPOInfo info(registry);
	harness.expectFalse(info.load(testDeviceGuid, otherDeviceGuid),
		"a not-present endpoint is rejected by load, which is how loadAllInfos keeps unplugged-and-forgotten hardware out of the device list");
}

void testLoadTreatsTheUndocumentedDisabledFlagAsDisabled(test::Harness& harness)
{
	FakeRegistry registry;
	// Disabled endpoints do not carry DEVICE_STATE_DISABLED in the registry;
	// they carry 0x10000000, which is why load tests both bits.
	seedRenderDevice(registry, 0x10000000);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid),
		"a disabled device still loads, it is only marked");
	harness.expect(info.isDisabled(),
		"0x10000000 is the value Windows actually writes for a disabled endpoint, so recognising only DEVICE_STATE_DISABLED would report it as enabled");
	harness.expectFalse(info.isUnplugged(),
		"the disabled bit must not be mistaken for the unplugged one");
}

void testLoadWithForeignApoGuidsReportsNotInstalled(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, sfxGuidValueName, vendorPreMixGuid);
	registry.seedString(fxPropertiesKey, mfxGuidValueName, vendorPostMixGuid);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid),
		"a device carrying someone else's APOs loads like any other");

	harness.expectFalse(info.isInstalled(),
		"the driver's own APO GUIDs are not ours, so nothing is installed");
	harness.expectFalse(info.isExperimental(),
		"the FxProperties key exists, so installing will not have to create it and the device is not experimental");
	// getOriginalAPO* answers for the mode the GUI has selected rather than the
	// one load inferred, and a bare load leaves the selection at its default,
	// so select the mode these slots belong to before asking.
	info.getSelectedInstallState().installMode = DeviceAPOInfo::INSTALL_SFX_MFX;
	harness.expect(info.getOriginalAPOPreMix() == vendorPreMixGuid,
		"the driver's SFX APO is what a chained install would have to keep running in front of ours, so it is reported as the original pre-mix APO");
	harness.expect(info.getOriginalAPOPostMix() == vendorPostMixGuid,
		"the MFX slot answers the same way for the post-mix half");

	const bool windows81OrNewer = WindowsVersion::isAtLeast(6, 3);
	harness.expectEqual(static_cast<int>(info.getCurrentInstallState().installMode),
		static_cast<int>(windows81OrNewer ? DeviceAPOInfo::INSTALL_SFX_EFX : DeviceAPOInfo::INSTALL_LFX_GFX),
		"a driver that supplies SFX/MFX gets the SFX/EFX mode on Windows 8.1 and newer; before that only LFX/GFX exists");
}

void testLoadInfersLfxGfxWhenTheDriverSuppliesOnlyLegacySlots(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, lfxGuidValueName, vendorPreMixGuid);
	registry.seedString(fxPropertiesKey, gfxGuidValueName, vendorPostMixGuid);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads");

	harness.expectEqual(static_cast<int>(info.getCurrentInstallState().installMode),
		static_cast<int>(DeviceAPOInfo::INSTALL_LFX_GFX),
		"when the driver populated only the legacy LFX/GFX slots, installing into SFX/EFX would bypass its effects, so the legacy mode is kept");
}

void testLoadInfersSfxMfxForACombinedBluetoothDevice(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, sfxGuidValueName, vendorPreMixGuid);
	// Windows 11 combines the two halves of a Bluetooth headset into one
	// endpoint, and EFX does not run on the result.
	registry.seedString(propertiesKey, combinedDeviceValueName, L"1");

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads");

	const bool windows81OrNewer = WindowsVersion::isAtLeast(6, 3);
	harness.expectEqual(static_cast<int>(info.getCurrentInstallState().installMode),
		static_cast<int>(windows81OrNewer ? DeviceAPOInfo::INSTALL_SFX_MFX : DeviceAPOInfo::INSTALL_LFX_GFX),
		"a combined Bluetooth endpoint falls back to SFX/MFX because its EFX slot is never reached");
}

void testLoadDetectsOurApoAndRecoversTheInstallState(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, sfxGuidValueName, ourPreMixGuid());
	registry.seedString(fxPropertiesKey, mfxGuidValueName, ourPostMixGuid());
	registry.seedString(childApoKey, versionValueName, installVersion);
	for (const wchar_t* valueName : allGuidValueNames)
		registry.seedString(childApoKey, valueName, APOGUID_NOVALUE);
	registry.seedString(childApoKey, preMixChildGuidValueName, L"");
	registry.seedString(childApoKey, postMixChildGuidValueName, L"");
	registry.seedString(childApoKey, allowSilentBufferValueName, L"false");

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads");

	harness.expect(info.isInstalled(),
		"finding our own pre-mix GUID in an FxProperties slot is what installed means");
	harness.expectFalse(info.isExperimental(),
		"an installed device is never reported as experimental, whatever its original FxProperties state was");
	harness.expectFalse(info.canBeUpgraded(),
		"the recorded version matches the one this build installs, so there is nothing to upgrade");

	const DeviceAPOInfo::InstallState& state = info.getCurrentInstallState();
	harness.expectEqual(static_cast<int>(state.installMode), static_cast<int>(DeviceAPOInfo::INSTALL_SFX_MFX),
		"our GUIDs sit in the SFX and MFX slots, so the mode is inferred from where they were found rather than from a stored setting");
	harness.expect(state.installPreMix,
		"the SFX slot holds our pre-mix APO, so the pre-mix half is installed");
	harness.expect(state.installPostMix,
		"the MFX slot holds our post-mix APO, so the post-mix half is installed");
	harness.expectFalse(state.useOriginalAPOPreMix,
		"an empty PreMixChild means no original APO was chained behind ours");
	harness.expectFalse(state.allowSilentBufferModification,
		"AllowSilentBufferModification was written as false, and anything other than the literal false counts as true");
	harness.expect(state.autoAdjust,
		"automatic adjustment stays on unless a DisableAutomaticAdjustment value says otherwise");
}

void testLoadRejectsAnInstallationFromANewerBuild(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, sfxGuidValueName, ourPreMixGuid());
	registry.seedString(childApoKey, versionValueName, L"3");

	DeviceAPOInfo info(registry);
	bool refused = false;
	try
	{
		info.load(testDeviceGuid, otherDeviceGuid);
	}
	catch (const RegistryException&)
	{
		refused = true;
	}

	harness.expect(refused,
		"a stored version this build does not know means a newer Equalizer APO owns the device; load refuses rather than rewriting an installation it cannot describe");
}

void testLoadTreatsAVersionlessInstallationAsUpgradable(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, lfxGuidValueName, ourPreMixGuid());
	registry.seedString(fxPropertiesKey, gfxGuidValueName, ourPostMixGuid());
	// Version 1 installations wrote no Version value at all; the mismatch is
	// what an upgrade is detected by.
	registry.seedString(childApoKey, lfxGuidValueName, vendorPreMixGuid);
	registry.seedString(childApoKey, gfxGuidValueName, APOGUID_NOVALUE);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads");

	harness.expect(info.isInstalled(),
		"an old installation is still an installation");
	harness.expect(info.canBeUpgraded(),
		"a Child APOs key with no Version value is version 1, which differs from the version this build installs");
	harness.expect(info.getPreMixChildGuid() == vendorPreMixGuid,
		"the version 1 layout stored the chained pre-mix APO in the LFX slot of the Child APOs key, and that is where the upgrade path reads it from");
}

// Installs onto a device whose driver supplied no FxProperties key at all, and
// returns the state that was asked for so the caller can compare against it.
DeviceAPOInfo::InstallState installOnBareDevice(test::Harness& harness, FakeRegistry& registry)
{
	seedRenderDevice(registry);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads before installing");

	DeviceAPOInfo::InstallState& selected = info.getSelectedInstallState();
	selected.installPreMix = true;
	selected.installPostMix = true;
	selected.useOriginalAPOPreMix = false;
	selected.useOriginalAPOPostMix = false;
	selected.installMode = DeviceAPOInfo::INSTALL_SFX_MFX;
	selected.allowSilentBufferModification = true;
	selected.autoAdjust = false;
	const DeviceAPOInfo::InstallState requested = selected;

	info.install();
	return requested;
}

void testInstallThenLoadRoundTripsTheInstallState(test::Harness& harness)
{
	FakeRegistry registry;
	const DeviceAPOInfo::InstallState requested = installOnBareDevice(harness, registry);

	harness.require(registry.keyExists(fxPropertiesKey),
		"install creates the FxProperties key the driver never made, which is the whole point of the experimental path");
	harness.expect(registry.readValue(fxPropertiesKey, sfxGuidValueName) == ourPreMixGuid(),
		"the pre-mix APO goes into the SFX slot for the SFX/MFX mode");
	harness.expect(registry.readValue(fxPropertiesKey, mfxGuidValueName) == ourPostMixGuid(),
		"the post-mix APO goes into the MFX slot for the SFX/MFX mode");
	harness.expect(registry.valueExists(fxPropertiesKey, sfxProcessingModesValueName),
		"a slot we populate also needs its processing-mode list, or the audio engine never instantiates the APO");
	harness.expect(registry.readValue(childApoKey, lfxGuidValueName) == APOGUID_NOKEY,
		"the !KEY sentinel records that there was no FxProperties key before us, so uninstall knows to remove the key rather than restore values");

	DeviceAPOInfo reloaded(registry);
	harness.require(reloaded.load(testDeviceGuid, otherDeviceGuid),
		"the device still loads after being installed onto");
	harness.expect(reloaded.isInstalled(),
		"a fresh load has to see the installation the previous object just wrote");
	harness.expectEqual(reloaded.getLastOperationReport().operation == DeviceInstallReport::Operation::None, true,
		"a freshly loaded object has performed no operation, so its report says so instead of describing somebody else's");
	harness.expect(!(reloaded.getCurrentInstallState() != requested),
		"every field of the install state survives the round trip through the registry; a field that is written but never read back would silently reset itself on the next start");
}

void testUninstallRemovesTheFxPropertiesKeyItCreated(test::Harness& harness)
{
	FakeRegistry registry;
	installOnBareDevice(harness, registry);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the installed device loads");
	info.uninstall();

	harness.expectFalse(registry.keyExists(fxPropertiesKey),
		"the !KEY sentinel says this installation created FxProperties, and nothing else moved in, so uninstall takes the key away with it");
	harness.expectFalse(registry.keyExists(childApoKey),
		"the per-device Child APOs key is ours alone and goes with the installation");
	harness.expectFalse(registry.keyExists(childApoPath),
		"the Child APOs parent is removed once the last device leaves it, so no empty branch is left in the machine's registry");
	harness.expect(registry.keyExists(propertiesKey),
		"uninstall touches nothing the audio driver owns; the endpoint's own Properties key has to survive");
}

void testUninstallKeepsFxPropertiesWhenWindowsPutItsOwnSubkeysThere(test::Harness& harness)
{
	FakeRegistry registry;
	installOnBareDevice(harness, registry);

	// Windows 11 24H2 (build 26100) started creating subkeys below
	// FxProperties. RegDeleteKeyExW refuses a key that has subkeys, so the
	// whole-key delete this uninstall used to do threw and left the endpoint
	// pointing at CLSIDs that were about to be unregistered (issue #189).
	registry.seedKey(fxPropertiesKey + L"\\UserDefinedProperties");

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the installed device loads");
	info.uninstall();

	harness.expect(registry.keyExists(fxPropertiesKey),
		"the key has an occupant that is not ours, so it stays; deleting it would take the OS subkey with it");
	harness.expect(registry.keyExists(fxPropertiesKey + L"\\UserDefinedProperties"),
		"the subkey Windows created is not ours to remove");
	harness.expectFalse(registry.valueExists(fxPropertiesKey, sfxGuidValueName),
		"every value this installation wrote is still removed, so the endpoint stops pointing at our APO");
	harness.expectFalse(registry.valueExists(fxPropertiesKey, sfxProcessingModesValueName),
		"the processing-mode list we added is ours too and goes with the GUID");
	harness.expectFalse(registry.valueExists(fxPropertiesKey, fxTitleValueName),
		"the title we wrote is removed, or the endpoint keeps claiming Equalizer APO is on it");

	bool wholeKeyDeleteRefused = false;
	try
	{
		registry.deleteKey(fxPropertiesKey);
	}
	catch (const RegistryException&)
	{
		wholeKeyDeleteRefused = true;
	}
	harness.expect(wholeKeyDeleteRefused,
		"a key with subkeys cannot be deleted at all, which is what makes the keyEmpty guard above load-bearing rather than decorative");
}

// Installs over a driver that already published its own APOs, which is the
// branch that has to back the driver's values up and remember them.
void installOverVendorApos(test::Harness& harness, FakeRegistry& registry)
{
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, lfxGuidValueName, vendorPreMixGuid);
	registry.seedString(fxPropertiesKey, gfxGuidValueName, vendorPostMixGuid);
	registry.seedString(APP_REGPATH, L"ConfigPath", L"C:\\ProgramData\\EqualizerAPO\\config");

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads before installing");

	DeviceAPOInfo::InstallState& selected = info.getSelectedInstallState();
	selected.installPreMix = true;
	selected.installPostMix = true;
	selected.useOriginalAPOPreMix = true;
	selected.useOriginalAPOPostMix = true;
	selected.installMode = DeviceAPOInfo::INSTALL_LFX_GFX;

	info.install();
}

void testInstallExportsTheDriverValuesBeforeOverwritingThem(test::Harness& harness)
{
	FakeRegistry registry;
	installOverVendorApos(harness, registry);

	harness.requireEqual(registry.exports().size(), size_t(1),
		"overwriting values the audio driver owns is done exactly once and only after they have been exported, so a user can put the device back by hand");
	const FakeRegistry::Export& exported = registry.exports().front();
	harness.expect(exported.key == fxPropertiesKey,
		"the export covers the key being overwritten");
	harness.requireEqual(exported.valueNames.size(), size_t(2),
		"only the values that were actually there are exported; absent slots have nothing to restore");
	harness.requireEqual(exported.values.size(), size_t(2),
		"the export carries one contents entry per exported name");
	harness.expect(exported.valueNames[0] == lfxGuidValueName && exported.valueNames[1] == gfxGuidValueName,
		"the exported names are the two slots the driver had filled");
	harness.expect(exported.values[0] == vendorPreMixGuid && exported.values[1] == vendorPostMixGuid,
		"the exported contents are the driver's GUIDs as they read before the overwrite");
	harness.expect(exported.filePath == L"C:\\ProgramData\\EqualizerAPO\\config\\backup_"
			+ deviceName + L"_" + connectionName + L".reg",
		"the backup lands in the configured config directory, named after the device, and the missing trailing separator is supplied rather than concatenated away");

	harness.expect(registry.readValue(childApoKey, lfxGuidValueName) == vendorPreMixGuid,
		"the driver's LFX GUID is copied into the Child APOs key, which is what uninstall restores from");
	harness.expect(registry.readValue(childApoKey, sfxGuidValueName) == APOGUID_NOVALUE,
		"a slot the driver left empty is recorded with the !VALUE sentinel, so uninstall deletes it instead of writing an empty GUID back");
	harness.expect(registry.readValue(childApoKey, preMixChildGuidValueName) == vendorPreMixGuid,
		"asking to keep the original APO chains it behind ours, so its GUID is stored as the pre-mix child");
	harness.expect(registry.readValue(fxPropertiesKey, lfxGuidValueName) == ourPreMixGuid(),
		"our pre-mix APO takes the LFX slot the driver used to hold");
}

void testUninstallRestoresTheDriverApoGuids(test::Harness& harness)
{
	FakeRegistry registry;
	installOverVendorApos(harness, registry);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the installed device loads");
	info.uninstall();

	harness.expect(registry.keyExists(fxPropertiesKey),
		"the driver owned this key before we arrived, so uninstall must not delete it");
	harness.expect(registry.readValue(fxPropertiesKey, lfxGuidValueName) == vendorPreMixGuid,
		"the driver's pre-mix APO is put back exactly as it was, or the device loses its vendor effects after an uninstall");
	harness.expect(registry.readValue(fxPropertiesKey, gfxGuidValueName) == vendorPostMixGuid,
		"the driver's post-mix APO is put back too");
	harness.expectFalse(registry.valueExists(fxPropertiesKey, sfxGuidValueName),
		"a slot recorded as !VALUE was empty before the install, so restoring it means deleting it, not writing the sentinel into the registry");
	harness.expectFalse(registry.keyExists(childApoKey),
		"the bookkeeping key is removed once its contents have been used");
}

void testInstallTakesOwnershipWhenFxPropertiesCannotBeCreated(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	// Some drivers ship an endpoint key whose ACL denies even an administrator
	// the right to add a subkey.
	registry.denyCreateKey(fxPropertiesKey);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads");
	DeviceAPOInfo::InstallState& selected = info.getSelectedInstallState();
	selected.installPreMix = true;
	selected.installPostMix = true;
	info.install();

	harness.requireEqual(registry.takeOwnershipCalls().size(), size_t(1),
		"a denied createKey is recovered from by taking ownership once, not by retrying blindly");
	harness.expect(registry.takeOwnershipCalls().front() == renderDeviceKey,
		"ownership is taken of the endpoint key, because that is the key whose ACL is refusing the new subkey");
	harness.requireEqual(registry.makeWritableCalls().size(), size_t(1),
		"owning the key is not the same as being allowed to write to it, so the ACL is rewritten as well");
	harness.expect(registry.keyExists(fxPropertiesKey),
		"the retry after the permission change is what actually creates the key; without it install would report success having written nothing");
}

// The failure this rework exists for. install() writes our CLSIDs into the
// endpoint last, one slot at a time, so a failure between the two used to leave
// a device with our pre-mix APO and the driver's post-mix APO - and load() then
// reported it as installed, because finding either CLSID is what installed
// means. Nobody, including this program, could describe that endpoint's audio
// chain afterwards.
void testInstallLeavesTheEndpointAloneWhenAMidwaySlotWriteFails(test::Harness& harness)
{
	FakeRegistry registry;
	seedRenderDevice(registry);
	registry.seedString(fxPropertiesKey, lfxGuidValueName, vendorPreMixGuid);
	registry.seedString(fxPropertiesKey, gfxGuidValueName, vendorPostMixGuid);
	registry.seedString(APP_REGPATH, L"ConfigPath", L"C:\\ProgramData\\EqualizerAPO\\config");
	// The post-mix slot refuses the write. On a real machine this is an ACL on
	// that one property, or a driver service holding the key.
	registry.failValueWrite(fxPropertiesKey, gfxGuidValueName);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the device loads before installing");
	DeviceAPOInfo::InstallState& selected = info.getSelectedInstallState();
	selected.installPreMix = true;
	selected.installPostMix = true;
	selected.useOriginalAPOPreMix = true;
	selected.useOriginalAPOPostMix = true;
	selected.installMode = DeviceAPOInfo::INSTALL_LFX_GFX;

	bool threw = false;
	try
	{
		info.install();
	}
	catch (const RegistryException&)
	{
		threw = true;
	}

	harness.expect(threw,
		"a failed install still reports the failure; the transaction changes what it leaves behind, not whether it complains");
	harness.expect(registry.readValue(fxPropertiesKey, lfxGuidValueName) == vendorPreMixGuid,
		"the slot that was written before the failure is put back to the driver's GUID, so the endpoint is not left half ours");
	harness.expect(registry.readValue(fxPropertiesKey, gfxGuidValueName) == vendorPostMixGuid,
		"the slot that refused the write never changed");
	harness.expectFalse(registry.keyExists(childApoKey),
		"the bookkeeping key this install created is gone with it, so no stale record of an installation that never happened survives");
	harness.expectFalse(registry.keyExists(childApoPath),
		"and neither does the parent key it had to create on the way");

	DeviceAPOInfo reloaded(registry);
	harness.require(reloaded.load(testDeviceGuid, otherDeviceGuid),
		"the device still loads after the failed install");
	harness.expectFalse(reloaded.isInstalled(),
		"the state a failed install leaves must not read as installed, which is the failure mode this whole change is about");

	// The report is the only thing a user can send after this: the message box is
	// closed by the time they ask for help, and before this there was nothing in
	// any log to look at.
	const DeviceInstallReport& report = info.getLastOperationReport();
	harness.expectEqual(static_cast<int>(report.outcome), static_cast<int>(DeviceInstallReport::Outcome::Failed),
		"a failed operation reports itself as failed rather than leaving the previous verdict standing");
	harness.expect(!report.failure.empty(),
		"the report carries the registry's own message, which is where the Win32 status is");
	harness.expect(!report.appliedOperations.empty(),
		"and how far it got, so a reader can see which slot it died on");
	harness.expectFalse(report.leftInconsistent(),
		"the rollback finished, so this device is not in the one state that needs a reboot to leave");
	harness.expect(report.fxPropertiesExisted,
		"the report says the driver had published its own FxProperties key, which is what makes the vendor-APO branch the one that ran");
	harness.expectEqual(report.driverSlots.size(), size_t(2),
		"and which slots the driver had filled, taken from what load() found rather than from the state after the failure");
	harness.expect(!report.backupPath.empty(),
		"the export happened before the failure, so its path is in the report - it is the file a user needs to restore the driver's chain by hand");
}

void testUninstallPutsTheInstallationBackWhenItCannotFinish(test::Harness& harness)
{
	FakeRegistry registry;
	installOverVendorApos(harness, registry);
	// Uninstall restores the driver's slots one at a time. The second one refuses
	// the write, so without a rollback the endpoint would keep our post-mix APO
	// while its pre-mix slot went back to the driver: installed and uninstalled at
	// the same time, and no run of either operation would agree on which.
	registry.failValueWrite(fxPropertiesKey, gfxGuidValueName);

	DeviceAPOInfo info(registry);
	harness.require(info.load(testDeviceGuid, otherDeviceGuid), "the installed device loads");

	bool threw = false;
	try
	{
		info.uninstall();
	}
	catch (const RegistryException&)
	{
		threw = true;
	}

	harness.expect(threw, "the failed uninstall reports its failure");
	harness.expect(registry.readValue(fxPropertiesKey, lfxGuidValueName) == ourPreMixGuid(),
		"a rolled-back uninstall leaves the installation in place; a device that is neither installed nor uninstalled is the state with no way out");
	harness.expect(registry.readValue(childApoKey, lfxGuidValueName) == vendorPreMixGuid,
		"the record of what the driver had is still there, so a later uninstall can still restore it");
	harness.expect(registry.readValue(childApoKey, versionValueName) == installVersion,
		"including the version stamp, without which load() would treat the installation as a version-1 upgrade candidate");

	DeviceAPOInfo reloaded(registry);
	harness.require(reloaded.load(testDeviceGuid, otherDeviceGuid), "the device loads after the failed uninstall");
	harness.expect(reloaded.isInstalled(),
		"and it reads as installed, which is what it still is");
}

void testCheckProtectedAudioDGReportsAndFixesTheDisabledFlag(test::Harness& harness)
{
	FakeRegistry registry;
	registry.seedKey(protectedDGKeyPath);

	harness.expectFalse(DeviceAPOInfo::checkProtectedAudioDG(false, registry),
		"with the value absent the protected audio path is still on, and no APO of ours would ever be loaded");
	harness.expectFalse(registry.valueExists(protectedDGKeyPath, protectedDGValueName),
		"asking without fix must not write anything, because the Editor calls it on every start just to warn");

	harness.expectFalse(DeviceAPOInfo::checkProtectedAudioDG(true, registry),
		"the fixing call still reports the state it found, so the caller can tell the user something was changed");
	harness.expectEqual(registry.readDWORDValue(protectedDGKeyPath, protectedDGValueName), 1ul,
		"fixing writes the flag that disables the protected audio path");
	harness.expect(DeviceAPOInfo::checkProtectedAudioDG(false, registry),
		"once the flag is set the check passes and stops nagging");
}
} // namespace

void runDeviceApoInfoTests(test::Harness& harness)
{
	testLoadWithoutFxPropertiesMarksTheDeviceExperimental(harness);
	testLoadSkipsDevicesTheDriverReportsAsNotPresent(harness);
	testLoadTreatsTheUndocumentedDisabledFlagAsDisabled(harness);
	testLoadWithForeignApoGuidsReportsNotInstalled(harness);
	testLoadInfersLfxGfxWhenTheDriverSuppliesOnlyLegacySlots(harness);
	testLoadInfersSfxMfxForACombinedBluetoothDevice(harness);
	testLoadDetectsOurApoAndRecoversTheInstallState(harness);
	testLoadRejectsAnInstallationFromANewerBuild(harness);
	testLoadTreatsAVersionlessInstallationAsUpgradable(harness);
	testInstallThenLoadRoundTripsTheInstallState(harness);
	testUninstallRemovesTheFxPropertiesKeyItCreated(harness);
	testUninstallKeepsFxPropertiesWhenWindowsPutItsOwnSubkeysThere(harness);
	testInstallExportsTheDriverValuesBeforeOverwritingThem(harness);
	testUninstallRestoresTheDriverApoGuids(harness);
	testInstallTakesOwnershipWhenFxPropertiesCannotBeCreated(harness);
	testInstallLeavesTheEndpointAloneWhenAMidwaySlotWriteFails(harness);
	testUninstallPutsTheInstallationBackWhenItCannotFinish(harness);
	testCheckProtectedAudioDGReportsAndFixesTheDisabledFlag(harness);
}
