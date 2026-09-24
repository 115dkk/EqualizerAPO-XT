/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	One install, uninstall or reinstall of a device record, applied in one
	RegistryTransaction and described in a DeviceInstallReport. The endpoint
	adapter always ran this way; the ASIO adapter wrote its record, both
	registry views of its entry and the Run value straight to the registry,
	so a failure midway left a record without an entry and nothing in the
	log (audit #348 C2/TD-31). Both adapters run their operations through
	here now.
*/

#pragma once

#include <functional>

#include "devices/DeviceInstallReport.h"

class IRegistry;
class RegistryTransaction;

namespace ReportedOperation
{
	// Runs steps inside one RegistryTransaction on registry. The caller has
	// already filled the report's identity (operation, device, what was found).
	//
	// On success: commits, records the applied operations and whether the
	// change was fully reversible, logs the summary line and the detail behind
	// trace. On any exception: rolls back first, so the report can carry what
	// the rollback could not put back, records the failure, logs the whole
	// report, and rethrows the original exception unchanged.
	void run(IRegistry& registry, DeviceInstallReport& report,
		const std::function<void(RegistryTransaction&)>& steps);
}
