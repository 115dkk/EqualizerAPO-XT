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


bool DeviceAPOInfo::canBeUpgraded() const
{
	return installed && version != installVersion;
}

bool DeviceAPOInfo::hasChanges() const
{
	return installed && selectedInstallState != currentInstallState;
}

bool DeviceAPOInfo::isExperimental() const
{
	return !installed && originalApoGuids[0] == APOGUID_NOKEY;
}

wstring DeviceAPOInfo::getOriginalAPOPreMix()
{
	wstring guid;
	switch (selectedInstallState.installMode)
	{
	case INSTALL_LFX_GFX:
		guid = originalApoGuids[LFX_INDEX];
		if (RegistryHelper::isWindowsVersionAtLeast(6, 3)) // Windows 8.1
		{
			if (originalApoGuids[LFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[GFX_INDEX] == APOGUID_NOVALUE)
				guid = originalApoGuids[SFX_INDEX];
		}
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
		if (RegistryHelper::isWindowsVersionAtLeast(6, 3)) // Windows 8.1
		{
			if (originalApoGuids[LFX_INDEX] == APOGUID_NOVALUE && originalApoGuids[GFX_INDEX] == APOGUID_NOVALUE)
				guid = originalApoGuids[MFX_INDEX];
		}
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

