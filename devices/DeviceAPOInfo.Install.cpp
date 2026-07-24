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
		{
			wstring backupDirectory = RegistryHelper::readValue(APP_REGPATH, L"ConfigPath");
			if (backupDirectory.empty())
				throw RegistryException(L"ConfigPath is empty; refusing to write a registry backup to the process directory");
			if (backupDirectory.back() != L'\\' && backupDirectory.back() != L'/')
				backupDirectory += L"\\";
			RegistryHelper::saveToFile(keyPath + L"\\FxProperties", valuenames,
				backupDirectory + L"backup_" + StringHelper::replaceIllegalCharacters(deviceName)
				+ L"_" + StringHelper::replaceIllegalCharacters(connectionName) + L".reg");
		}
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
