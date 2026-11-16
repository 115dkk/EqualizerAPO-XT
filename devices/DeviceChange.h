/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <vector>
#include "devices/AbstractAPOInfo.h"
#include "devices/DevicePlan.h"
#include "runtime/errors/WideError.h"

namespace DeviceChange
{
	enum class Context { Dialog, HeadlessInstall, HeadlessUninstall };

	struct Request
	{
		DevicePlan plan;
		AbstractAPOInfo* info = nullptr;
	};

	struct DeviceResult
	{
		DeviceAction action = DeviceAction::None;
		DeviceInstallReport report;
		std::wstring error;
	};

	struct Result
	{
		std::vector<DeviceResult> devices;
		bool restartAudio = false;
		bool startVoicemeeterClient = false;
		bool startResidentHost = false;
	};

	// Requests name live, non-null adapters. The dialog applies one row at a time
	// to show failures before continuing. Follow-ups are returned, never performed
	// here; the empty dialog still starts both clients (audit #467 TD-34).
	inline Result apply(const std::vector<Request>& requests, Context context)
	{
		Result result;
		result.startVoicemeeterClient = context == Context::Dialog;
		result.startResidentHost = context == Context::Dialog;
		for (const Request& request : requests)
		{
			DeviceResult device;
			device.action = request.plan.action;
			try
			{
				switch (request.plan.action)
				{
				case DeviceAction::Install: request.info->install(); break;
				case DeviceAction::Uninstall: request.info->uninstall(); break;
				case DeviceAction::Reinstall: request.info->reinstall(); break;
				case DeviceAction::None: break;
				}
				if (request.plan.changesSomething())
				{
					result.restartAudio = result.restartAudio || request.info->changesNeedAudioRestart();
					if (context == Context::HeadlessInstall)
						result.startResidentHost = true;
				}
			}
			catch (const WideError& e)
			{
				device.error = e.getMessage();
			}
			device.report = request.info->getLastOperationReport();
			result.devices.push_back(device);
		}
		return result;
	}
}
