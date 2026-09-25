/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace eapo::asio
{
	// IASIO::getErrorMessage requires room for 124 bytes, including the terminator.
	inline constexpr unsigned errorMessageBytes = 124;
	inline constexpr unsigned defaultDeadlinePercent = 25;
}
