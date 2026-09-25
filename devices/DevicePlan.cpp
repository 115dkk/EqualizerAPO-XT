/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/DevicePlan.h"

#include "devices/AbstractAPOInfo.h"

DeviceFacts deviceFactsOf(const AbstractAPOInfo& info)
{
	DeviceFacts facts;
	facts.installed = info.isInstalled();
	facts.canBeUpgraded = info.canBeUpgraded();
	facts.hasChanges = info.hasChanges();
	facts.enhancementsDisabled = info.isEnhancementsDisabled();
	return facts;
}

DevicePlan planFor(bool checked, const DeviceFacts& facts)
{
	DevicePlan plan;
	if (checked && !facts.installed)
	{
		plan.action = DeviceAction::Install;
	}
	else if (!checked && facts.installed)
	{
		plan.action = DeviceAction::Uninstall;
	}
	else if (checked)
	{
		// Installed and staying installed: reinstall when there is a reason,
		// the first one found being the one the state text names.
		if (facts.canBeUpgraded)
			plan.reason = DevicePlan::Reason::Upgrade;
		else if (facts.hasChanges)
			plan.reason = DevicePlan::Reason::Changes;
		else if (facts.enhancementsDisabled)
			plan.reason = DevicePlan::Reason::Enhancements;
		if (plan.reason != DevicePlan::Reason::None)
			plan.action = DeviceAction::Reinstall;
	}
	return plan;
}

DevicePlan planForHeadlessInstall(const DeviceFacts& facts)
{
	DevicePlan plan;
	if (facts.installed)
	{
		plan.action = DeviceAction::Reinstall;
		plan.reason = DevicePlan::Reason::Requested;
	}
	else
	{
		plan.action = DeviceAction::Install;
	}
	return plan;
}
