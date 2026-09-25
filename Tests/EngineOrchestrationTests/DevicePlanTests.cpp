/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What applying the Device Selector does to one device (devices/DevicePlan.h,
	audit #348 C9), case by case: the rule OK carries out, OK's enabled state,
	the upgrade notice and the row's state text all read.
*/

#include "devices/DevicePlan.h"
#include "Tests/TestHarness.h"

namespace
{
DeviceFacts facts(bool installed, bool upgrade = false, bool changes = false, bool enhancementsOff = false)
{
	DeviceFacts result;
	result.installed = installed;
	result.canBeUpgraded = upgrade;
	result.hasChanges = changes;
	result.enhancementsDisabled = enhancementsOff;
	return result;
}

void testPlanTable(test::Harness& harness)
{
	harness.expect(planFor(true, facts(false)).action == DeviceAction::Install, "a checked, uninstalled device is installed");
	harness.expect(planFor(false, facts(true)).action == DeviceAction::Uninstall, "an unchecked, installed device is uninstalled");
	harness.expect(planFor(false, facts(true, true, true, true)).action == DeviceAction::Uninstall,
		"unchecking wins over every reason to reinstall");
	harness.expect(!planFor(false, facts(false, true, true, true)).changesSomething(),
		"an unchecked device that is not installed is left alone, whatever else is true");
	harness.expect(!planFor(true, facts(true)).changesSomething(), "an installed device with nothing to change is left alone");

	const DevicePlan upgrade = planFor(true, facts(true, true, true, true));
	harness.expect(upgrade.action == DeviceAction::Reinstall && upgrade.reason == DevicePlan::Reason::Upgrade,
		"an older installation is reinstalled, and that is the reason named first");
	harness.expect(upgrade.isUpgrade(), "which is an upgrade the user did not ask for");

	const DevicePlan changes = planFor(true, facts(true, false, true, true));
	harness.expect(changes.action == DeviceAction::Reinstall && changes.reason == DevicePlan::Reason::Changes,
		"changed options are the next reason");
	harness.expectFalse(changes.isUpgrade(), "a change the user made is not an upgrade");

	const DevicePlan enhancements = planFor(true, facts(true, false, false, true));
	harness.expect(enhancements.action == DeviceAction::Reinstall && enhancements.reason == DevicePlan::Reason::Enhancements,
		"enhancements that Windows turned off are the last reason");
	harness.expect(enhancements.isUpgrade(), "and turning them back on is offered like an upgrade");
}

void testHeadlessInstall(test::Harness& harness)
{
	harness.expect(planForHeadlessInstall(facts(false)).action == DeviceAction::Install, "--install-endpoint installs");
	const DevicePlan again = planForHeadlessInstall(facts(true));
	harness.expect(again.action == DeviceAction::Reinstall && again.reason == DevicePlan::Reason::Requested,
		"and reinstalls an installed device even with nothing to change, so the requested mode applies");
	harness.expectFalse(again.isUpgrade(), "a requested reinstall is not an upgrade");
}
}

void runDevicePlanTests(test::Harness& harness)
{
	testPlanTable(harness);
	testHeadlessInstall(harness);
}
