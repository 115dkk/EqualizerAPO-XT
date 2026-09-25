/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What applying the Device Selector does to one device (audit #348 C9).

	The rule used to be written four times in DeviceSelector.cpp - what OK
	carries out, whether OK is enabled, whether the upgrade notice shows, and
	the row's state text - and a fifth time for the headless --install-endpoint.
	The copies agreed, but a new state (an ASIO entry's options, say) had to be
	added to all of them and nothing could test any of them. Qt-free, so the
	suites can.
*/

#pragma once

class AbstractAPOInfo;

enum class DeviceAction
{
	None,
	Install,
	Uninstall,
	Reinstall
};

// The facts about a device the plan depends on.
struct DeviceFacts
{
	bool installed = false;
	bool canBeUpgraded = false;
	bool hasChanges = false;
	bool enhancementsDisabled = false;
};

DeviceFacts deviceFactsOf(const AbstractAPOInfo& info);

struct DevicePlan
{
	// Why a reinstall happens, in the order the state text names the reasons.
	enum class Reason
	{
		None,
		Upgrade,
		Changes,
		Enhancements,
		// The headless --install-endpoint reinstalls an installed device
		// whatever its state, so the requested mode takes effect.
		Requested
	};

	DeviceAction action = DeviceAction::None;
	Reason reason = Reason::None;

	bool changesSomething() const
	{
		return action != DeviceAction::None;
	}

	// A reinstall the user did not ask for by editing the device: an older
	// installation, or audio enhancements that Windows turned off. The Device
	// Selector says so when it applies one.
	bool isUpgrade() const
	{
		return action == DeviceAction::Reinstall && (reason == Reason::Upgrade || reason == Reason::Enhancements);
	}
};

// The plan for a device whose row is checked or not.
DevicePlan planFor(bool checked, const DeviceFacts& facts);

// The plan for the headless --install-endpoint: install, or reinstall an
// installed device even when nothing about it would change.
DevicePlan planForHeadlessInstall(const DeviceFacts& facts);
