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

	registry.createKey(childApoPath);
	registry.createKey(childApoPath L"\\" + deviceGuid);

	wstring keyPath;
	if (!input)
		keyPath = renderKeyPath L"\\" + deviceGuid;
	else
		keyPath = captureKeyPath L"\\" + deviceGuid;

	if (!registry.keyExists(keyPath + L"\\FxProperties"))
	{
		try
		{
			registry.createKey(keyPath + L"\\FxProperties");
		}
		catch (const RegistryException&)
		{
			// Permissions were not sufficient, so change them
			registry.takeOwnership(keyPath);
			registry.makeWritable(keyPath);

			registry.createKey(keyPath + L"\\FxProperties");
		}

		registry.writeValue(keyPath + L"\\FxProperties", fxTitleValueName, L"Equalizer APO");

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			registry.writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], APOGUID_NOKEY);
		}
	}
	else
	{
		vector<wstring> valuenames;

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			wstring apoGuidString = APOGUID_NOVALUE;
			if (registry.valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				apoGuidString = registry.readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
				valuenames.push_back(allGuidValueNames[i]);
			}

			registry.writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], apoGuidString);
		}

		if (!valuenames.empty())
		{
			wstring backupDirectory = registry.readValue(APP_REGPATH, L"ConfigPath");
			if (backupDirectory.empty())
				throw RegistryException(L"ConfigPath is empty; refusing to write a registry backup to the process directory");
			if (backupDirectory.back() != L'\\' && backupDirectory.back() != L'/')
				backupDirectory += L"\\";
			registry.saveToFile(keyPath + L"\\FxProperties", valuenames,
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
	registry.writeValue(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName, preMixValue);
	registry.writeValue(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName, postMixValue);

	registry.writeValue(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName, selectedInstallState.allowSilentBufferModification ? L"true" : L"false");
	if (selectedInstallState.autoAdjust)
	{
		if (registry.valueExists(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName))
			registry.deleteValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName);
	}
	else
	{
		registry.writeValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName, L"true");
	}
	registry.writeValue(childApoPath L"\\" + deviceGuid, versionValueName, installVersion);

	if (selectedInstallState.installMode == INSTALL_LFX_GFX)
	{
		if (selectedInstallState.installPreMix)
			registry.writeValue(keyPath + L"\\FxProperties", lfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
		if (selectedInstallState.installPostMix && !input)
			registry.writeValue(keyPath + L"\\FxProperties", gfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
		if (registry.valueExists(keyPath + L"\\FxProperties", sfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", sfxGuidValueName);
		if (registry.valueExists(keyPath + L"\\FxProperties", mfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", mfxGuidValueName);
		if (registry.valueExists(keyPath + L"\\FxProperties", efxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", efxGuidValueName);
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_MFX)
	{
		if (registry.valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (registry.valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			registry.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!registry.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				registry.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		if (selectedInstallState.installPostMix && !input)
		{
			registry.writeValue(keyPath + L"\\FxProperties", mfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!registry.valueExists(keyPath + L"\\FxProperties", mfxProcessingModesValueName))
				registry.writeMultiValue(keyPath + L"\\FxProperties", mfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change efx
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_EFX)
	{
		if (registry.valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (registry.valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			registry.deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			registry.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!registry.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				registry.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change mfx
		if (selectedInstallState.installPostMix && !input)
		{
			registry.writeValue(keyPath + L"\\FxProperties", efxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!registry.valueExists(keyPath + L"\\FxProperties", efxProcessingModesValueName))
				registry.writeMultiValue(keyPath + L"\\FxProperties", efxProcessingModesValueName, defaultProcessingModeValue);
		}
	}

	// force-enable enhancements
	if (registry.valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		registry.deleteValue(keyPath + L"\\FxProperties", disableEnhancementsValueName);
}
