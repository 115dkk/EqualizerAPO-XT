#include "stdafx.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"

#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

#define protectedDGKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio"
#define protectedDGValueName L"DisableProtectedAudioDG"
#define apoRegistrationKeyPath L"HKEY_CLASSES_ROOT\\AudioEngine\\AudioProcessingObjects"
#define clsidKeyPath L"HKEY_CLASSES_ROOT\\CLSID"
#define commonKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio"
#define renderKeyPath commonKeyPath L"\\Render"
#define captureKeyPath commonKeyPath L"\\Capture"
#define childApoPath APP_REGPATH L"\\Child APOs"
static const wchar_t* preMixChildGuidValueName = L"PreMixChild";
static const wchar_t* postMixChildGuidValueName = L"PostMixChild";
static const wchar_t* allowSilentBufferValueName = L"AllowSilentBufferModification";
static const wchar_t* disableAutoAdjustValueName = L"DisableAutomaticAdjustment";
static const wchar_t* versionValueName = L"Version";
static const wchar_t* connectionValueName = L"{a45c254e-df1c-4efd-8020-67d146a850e0},2";
static const wchar_t* deviceValueName = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6";
static const wchar_t* combinedDeviceValueName = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},41";
static const wchar_t* formatValueName = L"{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0";
static const wchar_t* channelMaskValueName = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},3";
static const wchar_t* lfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1";
static const wchar_t* gfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
static const wchar_t* sfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
static const wchar_t* mfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
static const wchar_t* efxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
static const wchar_t* multiSfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13";
static const wchar_t* multiMfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14";
static const wchar_t* multiEfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15";
static const unsigned allGuidValueNameCount = 5;
static const wchar_t* allGuidValueNames[] = {lfxGuidValueName, gfxGuidValueName, sfxGuidValueName, mfxGuidValueName, efxGuidValueName};
enum GuidValueIndices
{
	LFX_INDEX = 0,
	GFX_INDEX = 1,
	SFX_INDEX = 2,
	MFX_INDEX = 3,
	EFX_INDEX = 4
};
static const wchar_t* fxTitleValueName = L"{b725f130-47ef-101a-a5f1-02608c9eebac},10";
static const wchar_t* sfxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5";
static const wchar_t* mfxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";
static const wchar_t* efxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";
static const wchar_t* defaultProcessingModeValue = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
static const wchar_t* disableEnhancementsValueName = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
static const wchar_t* installVersion = L"2";
static PROPERTYKEY guidPropertyKey = {{0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}, 4};


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
			WAVEFORMATEX* waveFormat = reinterpret_cast<WAVEFORMATEX*>(&format[8]);
			channelCount = waveFormat->nChannels;
			sampleRate = waveFormat->nSamplesPerSec;
			if (waveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
			{
				WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(waveFormat);
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
