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


void DeviceAPOInfo::install()
{
	if (!selectedInstallState.installPreMix && !selectedInstallState.installPostMix)
		return;

	RegistryHelper::createKey(childApoPath);
	RegistryHelper::createKey(childApoPath L"\\" + deviceGuid);

	wstring keyPath;
	if (!input)
		keyPath = renderKeyPath L"\\" + deviceGuid;
	else
		keyPath = captureKeyPath L"\\" + deviceGuid;

	if (!RegistryHelper::keyExists(keyPath + L"\\FxProperties"))
	{
		try
		{
			RegistryHelper::createKey(keyPath + L"\\FxProperties");
		}
		catch (const RegistryException&)
		{
			// Permissions were not sufficient, so change them
			RegistryHelper::takeOwnership(keyPath);
			RegistryHelper::makeWritable(keyPath);

			RegistryHelper::createKey(keyPath + L"\\FxProperties");
		}

		RegistryHelper::writeValue(keyPath + L"\\FxProperties", fxTitleValueName, L"Equalizer APO");

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], APOGUID_NOKEY);
		}
	}
	else
	{
		vector<wstring> valuenames;

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			wstring apoGuidString = APOGUID_NOVALUE;
			if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				apoGuidString = RegistryHelper::readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
				valuenames.push_back(allGuidValueNames[i]);
			}

			RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], apoGuidString);
		}

		if (!valuenames.empty())
			RegistryHelper::saveToFile(keyPath + L"\\FxProperties", valuenames,
				L"backup_" + StringHelper::replaceIllegalCharacters(deviceName) + L"_" + StringHelper::replaceIllegalCharacters(connectionName) + L".reg");
	}

	wstring preMixValue;
	wstring postMixValue;
	if (selectedInstallState.useOriginalAPOPreMix)
		preMixValue = getOriginalAPOPreMix();
	if (selectedInstallState.useOriginalAPOPostMix)
		postMixValue = getOriginalAPOPostMix();
	RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName, preMixValue);
	RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName, postMixValue);

	RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName, selectedInstallState.allowSilentBufferModification ? L"true" : L"false");
	if (selectedInstallState.autoAdjust)
	{
		if (RegistryHelper::valueExists(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName))
			RegistryHelper::deleteValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName);
	}
	else
	{
		RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName, L"true");
	}
	RegistryHelper::writeValue(childApoPath L"\\" + deviceGuid, versionValueName, installVersion);

	if (selectedInstallState.installMode == INSTALL_LFX_GFX)
	{
		if (selectedInstallState.installPreMix)
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", lfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
		if (selectedInstallState.installPostMix && !input)
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", gfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", sfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", sfxGuidValueName);
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", mfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", mfxGuidValueName);
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", efxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", efxGuidValueName);
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_MFX)
	{
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!RegistryHelper::valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				RegistryHelper::writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		if (selectedInstallState.installPostMix && !input)
		{
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", mfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!RegistryHelper::valueExists(keyPath + L"\\FxProperties", mfxProcessingModesValueName))
				RegistryHelper::writeMultiValue(keyPath + L"\\FxProperties", mfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change efx
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_EFX)
	{
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			RegistryHelper::deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!RegistryHelper::valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				RegistryHelper::writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change mfx
		if (selectedInstallState.installPostMix && !input)
		{
			RegistryHelper::writeValue(keyPath + L"\\FxProperties", efxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!RegistryHelper::valueExists(keyPath + L"\\FxProperties", efxProcessingModesValueName))
				RegistryHelper::writeMultiValue(keyPath + L"\\FxProperties", efxProcessingModesValueName, defaultProcessingModeValue);
		}
	}

	// force-enable enhancements
	if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		RegistryHelper::deleteValue(keyPath + L"\\FxProperties", disableEnhancementsValueName);
}
