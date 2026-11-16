/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

namespace winutil
{
	// Registry endpoint GUIDs need a flow prefix for IMMDeviceEnumerator::GetDevice
	// (audit #467 TD-05 TD-47). Accept GUIDs with or without braces.
	inline std::wstring endpointId(bool capture, const std::wstring& endpointGuid)
	{
		std::wstring id = capture ? L"{0.0.1.00000000}." : L"{0.0.0.00000000}.";
		if (!endpointGuid.empty() && endpointGuid.front() == L'{')
			return id + endpointGuid;
		return id + L"{" + endpointGuid + L"}";
	}
}
