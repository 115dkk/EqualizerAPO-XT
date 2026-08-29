/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "text/WideString.h"
#include "platform/windows/GuidText.h"
#include "services/registry/RegistryPaths.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"

#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"
#include "asio/AsioRegistration.h"
#include "asio/WrapperRecord.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

void DeviceAPOInfo::install()
{
	runReported(DeviceInstallReport::Operation::Install, [this](RegistryTransaction& plan) {
		installWithin(plan);
		applyAsioEntry(plan);
	});
}

void DeviceAPOInfo::runReported(DeviceInstallReport::Operation operation,
	const std::function<void(RegistryTransaction&)>& steps)
{
	RegistryTransaction plan(registry);
	beginReport(operation);

	try
	{
		steps(plan);
	}
	catch (const RegistryError& e)
	{
		failReport(plan, e.getMessage());
		throw;
	}
	catch (const DeviceException& e)
	{
		failReport(plan, e.getMessage());
		throw;
	}
	catch (...)
	{
		// Whatever it was, the endpoint still has to be put back and the report
		// still has to say what happened before the caller sees the exception.
		failReport(plan, L"an exception of an unexpected type");
		throw;
	}

	plan.commit();
	finishReport(plan);
}

void DeviceAPOInfo::beginReport(DeviceInstallReport::Operation operation)
{
	DeviceInstallReport report;
	report.operation = operation;
	report.deviceName = deviceName;
	report.connectionName = connectionName;
	report.deviceGuid = deviceGuid;
	report.input = input;

	// originalApoGuids is what load() found on the endpoint, so it is the record
	// of the state before this operation - which is exactly what a reader needs
	// to understand the rest of the report.
	report.fxPropertiesExisted = originalApoGuids[0] != APOGUID_NOKEY;
	if (report.fxPropertiesExisted)
	{
		static const wchar_t* const slotNames[] = {L"LFX", L"GFX", L"SFX", L"MFX", L"EFX"};
		for (unsigned i = 0; i < allGuidValueNameCount; i++)
		{
			// APOGUID_NOVALUE means the slot was empty, which is not worth a line.
			if (originalApoGuids[i] != APOGUID_NOVALUE && !originalApoGuids[i].empty())
				report.driverSlots.push_back(wstring(slotNames[i]) + L" = " + originalApoGuids[i]);
		}
	}

	// An uninstall is described by what is on the device now; the other two by
	// what was asked for.
	const InstallState& state = operation == DeviceInstallReport::Operation::Uninstall
		? currentInstallState : selectedInstallState;
	switch (state.installMode)
	{
	case INSTALL_LFX_GFX:
		report.requestedMode = L"LFX/GFX";
		break;
	case INSTALL_SFX_MFX:
		report.requestedMode = L"SFX/MFX";
		break;
	case INSTALL_SFX_EFX:
		report.requestedMode = L"SFX/EFX";
		break;
	}
	report.installPreMix = state.installPreMix;
	report.installPostMix = state.installPostMix;

	lastOperationReport = report;
}

void DeviceAPOInfo::finishReport(RegistryTransaction& plan)
{
	lastOperationReport.outcome = DeviceInstallReport::Outcome::Succeeded;
	lastOperationReport.appliedOperations = plan.appliedOperations();
	lastOperationReport.permissionsWidened = !plan.isFullyReversible();

	// The summary is worth a log line every time: it is how a support request
	// about a device that stopped working can be tied to the moment it was
	// installed. The registry detail goes behind trace, because it is long and
	// only interesting once something is wrong.
	LogF(L"%s", lastOperationReport.toSummaryLine().c_str());
	for (const wstring& line : lastOperationReport.toLines())
		TraceF(L"%s", line.c_str());
}

void DeviceAPOInfo::failReport(RegistryTransaction& plan, const wstring& failure)
{
	// Roll back here rather than letting the destructor do it, because the
	// report has to carry what the rollback could not put back, and the
	// destructor runs after this function is done.
	plan.rollback();

	lastOperationReport.outcome = DeviceInstallReport::Outcome::Failed;
	lastOperationReport.failure = failure;
	lastOperationReport.appliedOperations = plan.appliedOperations();
	lastOperationReport.rollbackFailures = plan.rollbackFailures();
	lastOperationReport.permissionsWidened = !plan.isFullyReversible();

	// A failure is logged in full: this is the block a user is asked for when
	// they report that installing did nothing, and until now there was nothing
	// to ask for.
	for (const wstring& line : lastOperationReport.toLines())
		LogF(L"%s", line.c_str());
}

namespace
{
std::vector<wstring> processingModesFor(bool input)
{
	if (input)
		return std::vector<wstring>(std::begin(captureProcessingModeValues), std::end(captureProcessingModeValues));
	return std::vector<wstring>(std::begin(renderProcessingModeValues), std::end(renderProcessingModeValues));
}
}

void DeviceAPOInfo::applyAsioEntry(RegistryTransaction& plan)
{
	removeAsioEntry(plan);
	if (!selectedInstallState.exclusiveModeEq || (!selectedInstallState.installPreMix && !selectedInstallState.installPostMix))
		return;

	// The wrapper DLL beside the product; the value the install hook writes.
	const wstring installPath = plan.valueExists(APP_REGPATH, L"InstallPath") ? plan.readValue(APP_REGPATH, L"InstallPath") : L"";
	if (installPath.empty())
		throw DeviceException(L"The ASIO entry needs the InstallPath value under HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO");

	const eapo::asio::AsioTarget target = eapo::asio::AsioRegistration::endpointTarget(deviceGuid, connectionName, deviceName);
	eapo::asio::WrapperRecord record;
	record.wrapperClsid = eapo::asio::AsioRegistration::wrapperClsidFor(deviceGuid);
	record.targetKind = eapo::asio::TargetKind::WasapiExclusive;
	record.targetClsid = deviceGuid;
	record.targetName = target.name;
	if (input)
		record.captureEndpoint = deviceGuid;
	else
		record.renderEndpoint = deviceGuid;
	record.options.processOutput = !input;
	record.options.processInput = input;
	eapo::asio::WrapperRecords::write(plan, record);
	eapo::asio::AsioRegistration::registerWrapper(plan, target, installPath + L"\\EqualizerAPOAsio.dll", L"");
	lastOperationReport.asioEntry = eapo::asio::AsioRegistration::entryNameFor(target.name);
}

void DeviceAPOInfo::removeAsioEntry(RegistryTransaction& plan)
{
	const eapo::asio::AsioTarget target = eapo::asio::AsioRegistration::endpointTarget(deviceGuid, connectionName, deviceName);
	eapo::asio::AsioRegistration::unregisterWrapper(plan, target);
	eapo::asio::WrapperRecords::remove(plan, eapo::asio::AsioRegistration::wrapperClsidFor(deviceGuid));
}

void DeviceAPOInfo::installWithin(RegistryTransaction& plan)
{
	if (!selectedInstallState.installPreMix && !selectedInstallState.installPostMix)
		return;

	// The mode list a slot we populate gets when the driver left none: every
	// mode an app can end up in for this direction (DeviceAPOInfoKeys.h). A
	// list the driver wrote is the driver's decision and stays.
	const std::vector<wstring> processingModes = processingModesFor(input);

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
		catch (const RegistryError&)
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
				throw RegistryError(L"ConfigPath is empty; refusing to write a registry backup to the process directory");
			if (backupDirectory.back() != L'\\' && backupDirectory.back() != L'/')
				backupDirectory += L"\\";
			const wstring backupPath = backupDirectory + L"backup_"
				+ text::replaceIllegalFilenameCharacters(deviceName)
				+ L"_" + text::replaceIllegalFilenameCharacters(connectionName) + L".reg";
			plan.saveToFile(keyPath + L"\\FxProperties", valuenames, backupPath);
			// The one report field the caller cannot derive from the transaction:
			// this file is what a user needs to put the driver's chain back by
			// hand, so its path has to survive the operation either way.
			lastOperationReport.backupPath = backupPath;
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
			plan.writeValue(keyPath + L"\\FxProperties", lfxGuidValueName, winutil::guidToString(EQUALIZERAPO_PRE_MIX_GUID));
		if (selectedInstallState.installPostMix && !input)
			plan.writeValue(keyPath + L"\\FxProperties", gfxGuidValueName, winutil::guidToString(EQUALIZERAPO_POST_MIX_GUID));
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
			plan.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, winutil::guidToString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, processingModes);
		}
		if (selectedInstallState.installPostMix && !input)
		{
			plan.writeValue(keyPath + L"\\FxProperties", mfxGuidValueName, winutil::guidToString(EQUALIZERAPO_POST_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", mfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", mfxProcessingModesValueName, processingModes);
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
			plan.writeValue(keyPath + L"\\FxProperties", sfxGuidValueName, winutil::guidToString(EQUALIZERAPO_PRE_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", sfxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", sfxProcessingModesValueName, processingModes);
		}
		// don't change mfx
		if (selectedInstallState.installPostMix && !input)
		{
			plan.writeValue(keyPath + L"\\FxProperties", efxGuidValueName, winutil::guidToString(EQUALIZERAPO_POST_MIX_GUID));
			if (!plan.valueExists(keyPath + L"\\FxProperties", efxProcessingModesValueName))
				plan.writeMultiValue(keyPath + L"\\FxProperties", efxProcessingModesValueName, processingModes);
		}
	}

	// force-enable enhancements
	if (plan.valueExists(keyPath + L"\\FxProperties", disableEnhancementsValueName))
		plan.deleteValue(keyPath + L"\\FxProperties", disableEnhancementsValueName);
}
