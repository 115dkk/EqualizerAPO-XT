/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <memory>
#include <string>
#include <vector>

class AbstractAPOInfo;
class IRegistry;
class EndpointProbe;

namespace DeviceEnumeration
{
	enum class Unreadable { SkipAndLog, ReportFailure };
	struct Result
	{
		std::vector<std::shared_ptr<AbstractAPOInfo>> infos;
		std::vector<std::wstring> failures;
	};

	enum class Purpose { Selection, Cleanup };
	// Cleanup retains physical Voicemeeter endpoints as well as client strips,
	// so every installed APO can be removed (audit #467 TD-34).
	Result enumerate(bool input, IRegistry& registry, const EndpointProbe& probe, Unreadable policy,
		Purpose purpose = Purpose::Selection);
}
