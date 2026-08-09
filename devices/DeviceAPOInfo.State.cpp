#include "stdafx.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"

#include "text/StringHelper.h"
#include "services/registry/RegistryHelper.h"
#include "platform/windows/WindowsVersion.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

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
		if (WindowsVersion::isAtLeast(6, 3)) // Windows 8.1
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
		if (WindowsVersion::isAtLeast(6, 3)) // Windows 8.1
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

