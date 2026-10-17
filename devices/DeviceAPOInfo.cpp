/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2012  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "platform/windows/GuidText.h"
#include "services/registry/RegistryPaths.h"
#include <mmdeviceapi.h>
#include <mmreg.h>

#include "DeviceAPOInfo.h"
#include "AsioAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"
#include "asio/WrapperRecord.h"

#include "services/registry/RegistryError.h"
#include "services/logging/Logging.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

DeviceAPOInfo::DeviceAPOInfo(IRegistry& registry, const EndpointProbe& probe)
	: registry(registry), probe(probe)
{
}

vector<shared_ptr<AbstractAPOInfo>> DeviceAPOInfo::loadAllInfos(bool input, IRegistry& registry)
{
	vector<shared_ptr<AbstractAPOInfo>> result;

	vector<wstring> deviceGuidStrings = registry.enumSubKeys(input ? captureKeyPath : renderKeyPath);
	wstring defaultDeviceGuid = systemEndpointProbe().defaultEndpointGuid(input);
	for (vector<wstring>::iterator it = deviceGuidStrings.begin(); it != deviceGuidStrings.end(); it++)
	{
		wstring deviceGuidString = *it;

		// One endpoint at a time, as the uninstall sweep does: an endpoint
		// whose keys cannot be read would otherwise throw the whole list away,
		// and the Editor builds its device list without a try. keyExists
		// answers true for a key that refuses to be opened (audit #348), so a
		// driver-locked FxProperties now throws here instead of loading as an
		// endpoint without a driver chain.
		try
		{
			shared_ptr<DeviceAPOInfo> info = make_shared<DeviceAPOInfo>(registry);
			if (info->load(deviceGuidString, defaultDeviceGuid))
			{
				info->selectedInstallState = info->currentInstallState;
				result.push_back(move(info));
			}
		}
		catch (const WideError& e)
		{
			LogFStatic(L"Skipping endpoint %s, its registry keys could not be read: %s",
				deviceGuidString.c_str(), e.getMessage().c_str());
		}
	}

	if (!input)
		VoicemeeterAPOInfo::prependInfos(result, registry);
	// One playback and one capture record per ASIO target driver, appended
	// so endpoints keep their order (and the default device stays first).
	AsioAPOInfo::appendInfos(result, input, registry);

	return result;
}

bool DeviceAPOInfo::load(const wstring& deviceGuid, wstring defaultDeviceGuid)
{
	if (!loadFromRegistry(deviceGuid))
		return false;

	if (defaultDeviceGuid == L"")
		defaultDeviceGuid = probe.defaultEndpointGuid(input);

	GUID guid1, guid2;
	if (SUCCEEDED(CLSIDFromString(deviceGuid.c_str(), &guid1)) && SUCCEEDED(CLSIDFromString(defaultDeviceGuid.c_str(), &guid2)))
		defaultDevice = (guid1 == guid2) != 0;
	else
		defaultDevice = false;
	return true;
}

bool DeviceAPOInfo::loadFromRegistry(const wstring& deviceGuid)
{
	defaultDevice = false;
	wstring keyPath;
	if (registry.keyExists(renderKeyPath L"\\" + deviceGuid))
	{
		keyPath = renderKeyPath L"\\" + deviceGuid;
		input = false;
	}
	else
	{
		keyPath = captureKeyPath L"\\" + deviceGuid;
		input = true;
	}

	unsigned long deviceState = registry.readDWORDValue(keyPath, L"DeviceState");
	if (deviceState & DEVICE_STATE_NOTPRESENT)
		return false;
	// somehow, disabled devices do not actually use DEVICE_STATE_DISABLED in registry but 0x10000000
	disabled = deviceState & DEVICE_STATE_DISABLED || deviceState & 0x10000000;
	unplugged = deviceState & DEVICE_STATE_UNPLUGGED;

	this->deviceGuid = deviceGuid;

	connectionName = registry.readValue(keyPath + L"\\Properties", connectionValueName);
	deviceName = registry.readValue(keyPath + L"\\Properties", deviceValueName);

	channelCount = 0;
	sampleRate = 0;
	channelMask = 0;
	if (registry.valueExists(keyPath + L"\\Properties", formatValueName))
	{
		std::vector<unsigned char> format = registry.readBinaryValue(keyPath + L"\\Properties", formatValueName);
		if (format.size() >= sizeof(WAVEFORMATEX) + 8)
		{
			const WAVEFORMATEX* waveFormat = reinterpret_cast<const WAVEFORMATEX*>(&format[8]);
			channelCount = waveFormat->nChannels;
			sampleRate = waveFormat->nSamplesPerSec;
			if (waveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
			{
				const WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(waveFormat);
				channelMask = waveFormatExtensible->dwChannelMask;
			}
		}
	}
	if (channelMask == 0 && registry.valueExists(keyPath + L"\\Properties", channelMaskValueName))
		channelMask = registry.readDWORDValue(keyPath + L"\\Properties", channelMaskValueName);

	enhancementsDisabled = false;
	if (registry.keyExists(keyPath + L"\\FxProperties") && registry.valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		enhancementsDisabled = registry.readDWORDValue(keyPath + L"\\FxProperties", disableEnhancementsValueName) != 0;

	installed = false;
	currentInstallState.installMode = INSTALL_LFX_GFX;
	version = L"0";
	preMixChildGuid = L"";
	postMixChildGuid = L"";
	currentInstallState.installPreMix = true;
	currentInstallState.installPostMix = !input;
	currentInstallState.useOriginalAPOPreMix = true;
	currentInstallState.useOriginalAPOPostMix = !input;
	currentInstallState.allowSilentBufferModification = false;
	currentInstallState.receiveFromEndpoints = false;
	currentInstallState.autoAdjust = true;
	currentInstallState.asioEntry = false;
	currentInstallState.asioEntryOptions = {};

	if (!registry.keyExists(keyPath + L"\\FxProperties"))
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
			originalApoGuids[i] = APOGUID_NOKEY;

		// The install mode stays at LFX/GFX on purpose. A driver that
		// publishes no effect chain is, as a rule, a legacy driver that
		// declares no signal processing modes, and the audio engine feeds
		// such a driver's streams through the legacy slots only: measured
		// on VB-CABLE's recording endpoint by the capture gate
		// (Invoke-CaptureGate.ps1), an APO in the LFX slot takes the stream
		// and the same APO registered in the SFX slot is never created for
		// it. Choosing the modern slots here, as the branch below does for a
		// chain without legacy entries, would silence the EQ on most
		// microphones and every virtual cable.
	}
	else
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			if (registry.valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				wstring originalApoGuid = registry.readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
				if (originalApoGuid == winutil::guidToString(EQUALIZERAPO_PRE_MIX_GUID) || originalApoGuid == winutil::guidToString(EQUALIZERAPO_POST_MIX_GUID))
					originalApoGuid = APOGUID_NOVALUE;
				originalApoGuids[i] = originalApoGuid;
			}
			else
			{
				originalApoGuids[i] = APOGUID_NOVALUE;
			}
		}

		bool found = false;
		bool foundAt[allGuidValueNameCount];
		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			foundAt[i] = false;

			if (registry.valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				wstring apoGuidString = registry.readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);

				GUID apoGuid;
				if (SUCCEEDED(CLSIDFromString(apoGuidString.c_str(), &apoGuid)))
				{
					if (apoGuid == EQUALIZERAPO_PRE_MIX_GUID || apoGuid == EQUALIZERAPO_POST_MIX_GUID)
					{
						foundAt[i] = true;
						found = true;
					}
				}
			}
		}

		if (found)
		{
			installed = true;

			if (registry.keyExists(childApoPath L"\\" + deviceGuid))
			{
				if (registry.valueExists(childApoPath L"\\" + deviceGuid, versionValueName))
				{
					version = registry.readValue(childApoPath L"\\" + deviceGuid, versionValueName);
					if (version != installVersion)
						throw DeviceException(L"Unsupported version of APO installation detected! Please uninstall newer Equalizer APO before using this version of Device Selector.");
				}
				else
				{
					version = L"1";
				}

				for (int i = 0; i < allGuidValueNameCount; i++)
				{
					if (registry.valueExists(childApoPath L"\\" + deviceGuid, allGuidValueNames[i]))
						originalApoGuids[i] = registry.readValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i]);
				}

				if (version == installVersion)
				{
					if (registry.valueExists(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName))
						preMixChildGuid = registry.readValue(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName);
					if (registry.valueExists(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName))
						postMixChildGuid = registry.readValue(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName);

					currentInstallState.installPreMix = foundAt[LFX_INDEX] || foundAt[SFX_INDEX];
					currentInstallState.installPostMix = foundAt[GFX_INDEX] || foundAt[MFX_INDEX] || foundAt[EFX_INDEX];

					if (preMixChildGuid == L"")
						currentInstallState.useOriginalAPOPreMix = false;
					if (postMixChildGuid == L"")
						currentInstallState.useOriginalAPOPostMix = false;

					if (foundAt[LFX_INDEX] || foundAt[GFX_INDEX])
						currentInstallState.installMode = INSTALL_LFX_GFX;
					else if (foundAt[EFX_INDEX])
						currentInstallState.installMode = INSTALL_SFX_EFX;
					else if (foundAt[SFX_INDEX] || foundAt[MFX_INDEX])
						currentInstallState.installMode = INSTALL_SFX_MFX;

					if (registry.valueExists(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName))
						currentInstallState.allowSilentBufferModification = registry.readValue(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName) != L"false";
					if (!input && registry.valueExists(childApoPath L"\\" + deviceGuid, receiveFromEndpointsValueName))
						currentInstallState.receiveFromEndpoints = registry.readValue(childApoPath L"\\" + deviceGuid, receiveFromEndpointsValueName) != L"false";
					if (registry.valueExists(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName))
						currentInstallState.autoAdjust = registry.readValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName) == L"false";
				}
				else
				{
					if (registry.valueExists(childApoPath L"\\" + deviceGuid, lfxGuidValueName))
					{
						preMixChildGuid = originalApoGuids[0];
						postMixChildGuid = originalApoGuids[1];
					}
					else
					{
						preMixChildGuid = originalApoGuids[2];
						postMixChildGuid = originalApoGuids[3];
					}
				}
			}
			else if (registry.keyExists(childApoPath) && registry.valueExists(childApoPath, deviceGuid))
			{
				for (int i = 0; i < allGuidValueNameCount; i++)
				{
					if (foundAt[i])
					{
						originalApoGuids[i] = registry.readValue(childApoPath, deviceGuid);
						if (i == LFX_INDEX || i == SFX_INDEX)
							preMixChildGuid = originalApoGuids[i];
						else
							postMixChildGuid = originalApoGuids[i];
						break;
					}
				}
			}
		}
		else
		{
			// The minimum supported Windows is 10 1809, the oldest Qt 6.10 runs
			// on, so the SFX/MFX/EFX slots (Windows 8.1 and newer) always exist;
			// the branch that kept LFX/GFX for older systems is gone (audit #348
			// TD-53).
			// only use LFX/GFX if the audio driver supplied only those APOs
			if (registry.keyExists(keyPath + L"\\FxProperties")
				&& (registry.valueExists(keyPath + L"\\FxProperties", lfxGuidValueName) || registry.valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
				&& !registry.valueExists(keyPath + L"\\FxProperties", sfxGuidValueName)
				&& !registry.valueExists(keyPath + L"\\FxProperties", mfxGuidValueName)
				&& !registry.valueExists(keyPath + L"\\FxProperties", efxGuidValueName)
				&& !registry.valueExists(keyPath + L"\\FxProperties", multiSfxGuidValueName)
				&& !registry.valueExists(keyPath + L"\\FxProperties", multiMfxGuidValueName)
				&& !registry.valueExists(keyPath + L"\\FxProperties", multiEfxGuidValueName))
				currentInstallState.installMode = INSTALL_LFX_GFX;
			// bluetooth devices may be combined in Windows 11, EFX will not work then
			else if (registry.valueExists(keyPath + L"\\Properties", combinedDeviceValueName))
				currentInstallState.installMode = INSTALL_SFX_MFX;
			else
				currentInstallState.installMode = INSTALL_SFX_EFX;
		}
	}

	// The ASIO entry is the endpoint's own wrapper record, keyed by the
	// CLSID derived from the endpoint GUID; a driver record could never
	// carry that CLSID, so the kind check is belt and braces. A record that
	// cannot be read reads as no entry: it used to throw away everything
	// read above, the APO's child APO and capture flag included (audit #348
	// TD-47).
	try
	{
		eapo::asio::WrapperRecord record;
		currentInstallState.asioEntry = eapo::asio::WrapperRecords::read(registry, eapo::asio::AsioRegistration::wrapperClsidFor(deviceGuid), record)
			&& record.targetKind == eapo::asio::TargetKind::WasapiExclusive;
		if (currentInstallState.asioEntry)
			currentInstallState.asioEntryOptions = eapo::asio::WrapperRecords::entryOptions(record);
	}
	catch (const RegistryError& e)
	{
		currentInstallState.asioEntry = false;
		LogFStatic(L"Could not read the ASIO entry of endpoint %s: %s", deviceGuid.c_str(), e.getMessage().c_str());
	}

	return true;
}

bool DeviceAPOInfo::canBeUpgraded() const
{
	return installed && version != installVersion;
}

bool DeviceAPOInfo::hasChanges() const
{
	return installed && selectedInstallState != currentInstallState;
}

bool DeviceAPOInfo::hasDriverEffectChain() const
{
	return originalApoGuids[0] != APOGUID_NOKEY;
}

wstring DeviceAPOInfo::getOriginalAPOPreMix()
{
	wstring guid;
	switch (selectedInstallState.installMode)
	{
	case INSTALL_LFX_GFX:
		guid = originalApoGuids[LFX_INDEX];
		// Windows 10 1809 is the minimum, so the SFX slot always exists to fall
		// back to (the check for Windows 8.1 is gone, audit #348 TD-53).
		if (originalApoGuids[LFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[GFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[SFX_INDEX];
		break;
	case INSTALL_SFX_MFX:
		guid = originalApoGuids[SFX_INDEX];
		if (originalApoGuids[SFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[MFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[LFX_INDEX];
		break;
	case INSTALL_SFX_EFX:
		guid = originalApoGuids[SFX_INDEX];
		if (originalApoGuids[SFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[EFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[LFX_INDEX];
		break;
	}

	if (guid == APOGUID_NOKEY || guid == APOGUID_NOVALUE)
		guid = L"";

	return guid;
}

wstring DeviceAPOInfo::getOriginalAPOPostMix()
{
	wstring guid;
	switch (selectedInstallState.installMode)
	{
	case INSTALL_LFX_GFX:
		guid = originalApoGuids[GFX_INDEX];
		if (originalApoGuids[LFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[GFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[MFX_INDEX];
		break;
	case INSTALL_SFX_MFX:
		guid = originalApoGuids[MFX_INDEX];
		if (originalApoGuids[SFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[MFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[GFX_INDEX];
		break;
	case INSTALL_SFX_EFX:
		guid = originalApoGuids[EFX_INDEX];
		if (originalApoGuids[SFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[EFX_INDEX] == APOGUID_NOVALUE)
			guid = originalApoGuids[GFX_INDEX];
		break;
	}

	if (guid == APOGUID_NOKEY || guid == APOGUID_NOVALUE)
		guid = L"";

	return guid;
}


wstring DeviceAPOInfo::getConnectionName() const
{
	return connectionName;
}

wstring DeviceAPOInfo::getDeviceName() const
{
	return deviceName;
}

wstring DeviceAPOInfo::getDeviceGuid() const
{
	return deviceGuid;
}

wstring DeviceAPOInfo::getDeviceString() const
{
	return getConnectionName() + L" " + getDeviceName() + L" " + getDeviceGuid();
}

unsigned DeviceAPOInfo::getChannelCount() const
{
	return channelCount;
}

unsigned DeviceAPOInfo::getSampleRate() const
{
	return sampleRate;
}

unsigned long DeviceAPOInfo::getChannelMask() const
{
	return channelMask;
}

bool DeviceAPOInfo::isInput() const
{
	return input;
}

bool DeviceAPOInfo::isInstalled() const
{
	return installed;
}

bool DeviceAPOInfo::isEnhancementsDisabled() const
{
	return enhancementsDisabled;
}

bool DeviceAPOInfo::isDefaultDevice() const
{
	return defaultDevice;
}

bool DeviceAPOInfo::isDisabled() const
{
	return disabled;
}

bool DeviceAPOInfo::isUnplugged() const
{
	return unplugged;
}

const DeviceAPOInfo::InstallState& DeviceAPOInfo::getCurrentInstallState()
{
	return currentInstallState;
}

DeviceAPOInfo::InstallState& DeviceAPOInfo::getSelectedInstallState()
{
	return selectedInstallState;
}

const wstring& DeviceAPOInfo::getPreMixChildGuid() const
{
	return preMixChildGuid;
}

const wstring& DeviceAPOInfo::getPostMixChildGuid() const
{
	return postMixChildGuid;
}


wstring DeviceAPOInfo::getDefaultDevice(bool input, int role)
{
	return systemEndpointProbe().defaultEndpointGuid(input, role);
}

void DeviceAPOInfo::testAPOInstallation()
{
	probe.probeEndpoint(deviceGuid, input, deviceName);
}
