/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>
#include <type_traits>

namespace eapo::asio
{
	// IASIO::getErrorMessage requires room for 124 bytes, including the terminator.
	inline constexpr unsigned errorMessageBytes = 124;
	inline constexpr unsigned defaultDeadlinePercent = 25;

	template<size_t Size>
	inline void copyErrorMessage(char (&destination)[Size], const char* source) noexcept
	{
		size_t i = 0;
		if (source != nullptr)
			for (; i + 1 < Size && source[i] != '\0'; i++)
				destination[i] = source[i];
		destination[i] = '\0';
	}

	template<class Enum>
	constexpr unsigned slotOf(Enum value) noexcept
	{
		static_assert(std::is_enum_v<Enum>);
		return static_cast<unsigned>(value);
	}
}
