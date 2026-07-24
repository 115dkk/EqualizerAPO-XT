#include "stdafx.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"

#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

bool DeviceAPOInfo::load(const wstring& deviceGuid, wstring defaultDeviceGuid)
{
	wstring keyPath;
	if (RegistryHelper::keyExists(renderKeyPath L"\\" + deviceGuid))
	{
		keyPath = renderKeyPath L"\\" + deviceGuid;
		input = false;
	}
	else
	{
		keyPath = captureKeyPath L"\\" + deviceGuid;
		input = true;
	}

	unsigned long deviceState = RegistryHelper::readDWORDValue(keyPath, L"DeviceState");
	if (deviceState & DEVICE_STATE_NOTPRESENT)
		return false;
	// somehow, disabled devices do not actually use DEVICE_STATE_DISABLED in registry but 0x10000000
	disabled = deviceState & DEVICE_STATE_DISABLED || deviceState & 0x10000000;
	unplugged = deviceState & DEVICE_STATE_UNPLUGGED;

	this->deviceGuid = deviceGuid;

	connectionName = RegistryHelper::readValue(keyPath + L"\\Properties", connectionValueName);
	deviceName = RegistryHelper::readValue(keyPath + L"\\Properties", deviceValueName);

	channelCount = 0;
	sampleRate = 0;
	channelMask = 0;
	if (RegistryHelper::valueExists(keyPath + L"\\Properties", formatValueName))
	{
		std::vector<unsigned char> format = RegistryHelper::readBinaryValue(keyPath + L"\\Properties", formatValueName);
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
	if (channelMask == 0 && RegistryHelper::valueExists(keyPath + L"\\Properties", channelMaskValueName))
		channelMask = RegistryHelper::readDWORDValue(keyPath + L"\\Properties", channelMaskValueName);

	if (defaultDeviceGuid == L"")
		defaultDeviceGuid = getDefaultDevice(input);

	GUID guid1, guid2;
	if (SUCCEEDED(CLSIDFromString(deviceGuid.c_str(), &guid1)) && SUCCEEDED(CLSIDFromString(defaultDeviceGuid.c_str(), &guid2)))
		defaultDevice = (guid1 == guid2) != 0;
	else
		defaultDevice = false;

	enhancementsDisabled = false;
	if (RegistryHelper::keyExists(keyPath + L"\\FxProperties") && RegistryHelper::valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		enhancementsDisabled = RegistryHelper::readDWORDValue(keyPath + L"\\FxProperties", disableEnhancementsValueName) != 0;

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
	currentInstallState.autoAdjust = true;

	if (!RegistryHelper::keyExists(keyPath + L"\\FxProperties"))
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
			originalApoGuids[i] = APOGUID_NOKEY;
	}
	else
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				wstring originalApoGuid = RegistryHelper::readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
				if (originalApoGuid == RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID) || originalApoGuid == RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID))
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

			if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				wstring apoGuidString = RegistryHelper::readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);

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

			if (RegistryHelper::keyExists(childApoPath L"\\" + deviceGuid))
			{
				if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, versionValueName))
				{
					version = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, versionValueName);
					if (version != installVersion)
						throw RegistryException(L"Unsupported version of APO installation detected! Please uninstall newer Equalizer APO before using this version of Device Selector.");
				}
				else
				{
					version = L"1";
				}

				for (int i = 0; i < allGuidValueNameCount; i++)
				{
					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, allGuidValueNames[i]))
						originalApoGuids[i] = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i]);
				}

				if (version == installVersion)
				{
					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName))
						preMixChildGuid = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName);
					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName))
						postMixChildGuid = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName);

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

					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName))
						currentInstallState.allowSilentBufferModification = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName) != L"false";
					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName))
						currentInstallState.autoAdjust = RegistryHelper::readValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName) == L"false";
				}
				else
				{
					if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, lfxGuidValueName))
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
			else if (RegistryHelper::keyExists(childApoPath) && RegistryHelper::valueExists(childApoPath, deviceGuid))
			{
				for (int i = 0; i < allGuidValueNameCount; i++)
				{
					if (foundAt[i])
					{
						originalApoGuids[i] = RegistryHelper::readValue(childApoPath, deviceGuid);
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
			if (RegistryHelper::isWindowsVersionAtLeast(6, 3)) // Windows 8.1
			{
				// only use LFX/GFX if the audio driver supplied only those APOs
				if (RegistryHelper::keyExists(keyPath + L"\\FxProperties")
					&& (RegistryHelper::valueExists(keyPath + L"\\FxProperties", lfxGuidValueName) || RegistryHelper::valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", sfxGuidValueName)
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", mfxGuidValueName)
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", efxGuidValueName)
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", multiSfxGuidValueName)
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", multiMfxGuidValueName)
					&& !RegistryHelper::valueExists(keyPath + L"\\FxProperties", multiEfxGuidValueName))
					currentInstallState.installMode = INSTALL_LFX_GFX;
				// bluetooth devices may be combined in Windows 11, EFX will not work then
				else if (RegistryHelper::valueExists(keyPath + L"\\Properties", combinedDeviceValueName))
					currentInstallState.installMode = INSTALL_SFX_MFX;
				else
					currentInstallState.installMode = INSTALL_SFX_EFX;
			}
		}
	}

	return true;
}
