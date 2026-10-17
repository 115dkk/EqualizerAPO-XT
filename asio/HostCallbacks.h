/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "asio/AsioSdk.h"

namespace eapo::asio
{
	struct HostCallbacks
	{
		ASIOCallbacks calls = {};
		bool present = false;
		bool supportsTimeInfo = false;
		bool supportsResetRequest = false;

		void set(const ASIOCallbacks& source) noexcept
		{
			calls = source;
			present = true;
			supportsTimeInfo = calls.bufferSwitchTimeInfo != nullptr && calls.asioMessage != nullptr
				&& calls.asioMessage(kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
				&& calls.asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
			supportsResetRequest = calls.asioMessage != nullptr
				&& calls.asioMessage(kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) == 1;
		}

		void clear() noexcept
		{
			*this = HostCallbacks();
		}
	};
}
