/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	See DeviceInstallReport.h. Only the formatting lives here.
*/

#include "stdafx.h"

#include "DeviceInstallReport.h"

using std::vector;
using std::wstring;

namespace
{
const wchar_t* operationName(DeviceInstallReport::Operation operation)
{
	switch (operation)
	{
	case DeviceInstallReport::Operation::Install:
		return L"install";
	case DeviceInstallReport::Operation::Uninstall:
		return L"uninstall";
	case DeviceInstallReport::Operation::Reinstall:
		return L"repair";
	case DeviceInstallReport::Operation::None:
		break;
	}
	return L"no operation";
}

const wchar_t* outcomeName(DeviceInstallReport::Outcome outcome)
{
	switch (outcome)
	{
	case DeviceInstallReport::Outcome::Succeeded:
		return L"ok";
	case DeviceInstallReport::Outcome::Failed:
		return L"FAILED";
	case DeviceInstallReport::Outcome::NotAttempted:
		break;
	}
	return L"not attempted";
}
}

wstring DeviceInstallReport::toSummaryLine() const
{
	wstring line = wstring(operationName(operation)) + L" " + connectionName + L" " + deviceName
		+ L" (" + deviceGuid + L"): " + outcomeName(outcome);
	if (!failure.empty())
		line += L" - " + failure;
	return line;
}

vector<wstring> DeviceInstallReport::toLines() const
{
	vector<wstring> lines;
	lines.push_back(toSummaryLine());

	if (operation == Operation::None)
		return lines;

	lines.push_back(wstring(L"  direction: ") + (input ? L"capture" : L"render"));
	lines.push_back(wstring(L"  driver published FxProperties: ") + (fxPropertiesExisted ? L"yes" : L"no (Equalizer APO creates the effect chain)"));

	if (driverSlots.empty())
	{
		lines.push_back(L"  driver APO slots in use: none");
	}
	else
	{
		lines.push_back(L"  driver APO slots in use:");
		for (const wstring& slot : driverSlots)
			lines.push_back(L"    " + slot);
	}

	if (!requestedMode.empty())
	{
		wstring requested = L"  requested: " + requestedMode;
		if (installPreMix && installPostMix)
			requested += L", pre-mix and post-mix";
		else if (installPreMix)
			requested += L", pre-mix only";
		else if (installPostMix)
			requested += L", post-mix only";
		else
			requested += L", neither stage";
		lines.push_back(requested);
	}

	if (!asioEntry.empty())
		lines.push_back(L"  ASIO entry: " + asioEntry);

	if (!backupPath.empty())
		lines.push_back(L"  driver chain exported to: " + backupPath);

	if (permissionsWidened)
		lines.push_back(L"  the endpoint key's permissions were widened, which a rollback cannot undo");

	if (appliedOperations.empty())
	{
		lines.push_back(L"  registry changes: none");
	}
	else
	{
		lines.push_back(L"  registry changes, in order:");
		for (const wstring& applied : appliedOperations)
			lines.push_back(L"    " + applied);
	}

	if (!rollbackFailures.empty())
	{
		// The loud case. Everything else in this report describes a device that is
		// either installed or not; this describes one that is neither.
		lines.push_back(L"  THE ROLLBACK DID NOT FINISH. This endpoint may be left partly changed:");
		for (const wstring& rollbackFailure : rollbackFailures)
			lines.push_back(L"    " + rollbackFailure);
	}

	return lines;
}
