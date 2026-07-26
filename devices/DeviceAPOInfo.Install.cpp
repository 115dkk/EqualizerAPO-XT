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
	RegistryTransaction plan(registry);
	installWithin(plan);
	plan.commit();
}

void DeviceAPOInfo::installWithin(RegistryTransaction& plan)
{
	if (!selectedInstallState.installPreMix && !selectedInstallState.installPostMix)
		return;

	plan.createKey(childApoPath);
	plan.createKey(childApoPath L"\\" + deviceGuid);

	wstring keyPath;
	if (!input)
		keyPath = renderKeyPath L"\\" + deviceGuid;
	else
		keyPath = captureKeyPath L"\\" + deviceGuid;

	if (!plan.keyExists(keyPath + L"\\FxProperties"))
	{
		try
		{
			plan.createKey(keyPath + L"\\FxProperties");
		}
		catch (const RegistryException&)
		{
			// Permissions were not sufficient, so change them. This is the one
			// step the transaction cannot take back; see the note in
			// DeviceAPOInfo.h on what a failed install leaves behind.
			plan.takeOwnership(keyPath);
			plan.makeWritable(keyPath);

			plan.createKey(keyPath + L"\\FxProperties");
		}

		plan.writeValue(keyPath + L"\\FxProperties", fxTitleValueName, L"Equalizer APO");

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			plan.writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], APOGUID_NOKEY);
		}
	}
	else
	{
		vector<wstring> valuenames;

		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			wstring apoGuidString = APOGUID_NOVALUE;
			if (plan.valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
			{
				apoGuidString = plan.readValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
				valuenames.push_back(allGuidValueNames[i]);
			}

			plan.writeValue(childApoPath L"\\" + deviceGuid, allGuidValueNames[i], apoGuidString);
		}

		if (!valuenames.empty())
		{
			wstring backupDirectory = plan.readValue(APP_REGPATH, L"ConfigPath");
			if (backupDirectory.empty())
				throw RegistryException(L"ConfigPath is empty; refusing to write a registry backup to the process directory");
			if (backupDirectory.back() != L'\\' && backupDirectory.back() != L'/')
				backupDirectory += L"\\";
			plan.saveToFile(keyPath + L"\\FxProperties", valuenames,
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
	plan.writeValue(childApoPath L"\\" + deviceGuid, preMixChildGuidValueName, preMixValue);
	plan.writeValue(childApoPath L"\\" + deviceGuid, postMixChildGuidValueName, postMixValue);

	plan.writeValue(childApoPath L"\\" + deviceGuid, allowSilentBufferValueName, selectedInstallState.allowSilentBufferModification ? L"true" : L"false");
	if (selectedInstallState.autoAdjust)
	{
		if (plan.valueExists(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName))
			plan.deleteValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName);
	}
	else
	{
		plan.writeValue(childApoPath L"\\" + deviceGuid, disableAutoAdjustValueName, L"true");
	}
	plan.writeValue(childApoPath L"\\" + deviceGuid, versionValueName, installVersion);

	if (selectedInstallState.installMode == INSTALL_LFX_GFX)
	{
		if (selectedInstallState.installPreMix)
			plan.writeValue(keyPath + L"\\FxProperties", lfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
		if (selectedInstallState.installPostMix && !input)
			plan.writeValue(keyPath + L"\\FxProperties", gfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
		if (plan.valueExists(keyPath + L"\\FxProperties", sfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", sfxGuidValueName);
		if (plan.valueExists(keyPath + L"\\FxProperties", mfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", mfxGuidValueName);
		if (plan.valueExists(keyPath + L"\\FxProperties", efxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", efxGuidValueName);
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_MFX)
	{
		if (plan.valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (plan.valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			plan.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		if (selectedInstallState.installPostMix && !input)
		{
			plan.writeValue(keyPath + L"\\FxProperties", mfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", mfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", mfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change efx
	}
	else if (selectedInstallState.installMode == INSTALL_SFX_EFX)
	{
		if (plan.valueExists(keyPath + L"\\FxProperties", lfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", lfxGuidValueName);
		if (plan.valueExists(keyPath + L"\\FxProperties", gfxGuidValueName))
			plan.deleteValue(keyPath + L"\\FxProperties", gfxGuidValueName);
		if (selectedInstallState.installPreMix)
		{
			plan.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, defaultProcessingModeValue);
		}
		// don't change mfx
		if (selectedInstallState.installPostMix && !input)
		{
			plan.writeValue(keyPath + L"\\FxProperties", efxGuidValueName, RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", efxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", efxProcessingModesValueName, defaultProcessingModeValue);
		}
	}

	// force-enable enhancements
	if (plan.valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		plan.deleteValue(keyPath + L"\\FxProperties", disableEnhancementsValueName);
}
