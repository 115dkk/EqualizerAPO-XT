/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#pragma once

#include <utility>

namespace lifetime
{
	struct ScopeExitTag
	{
	};

	template<typename Function>
	class ScopeExit
	{
	public:
		explicit ScopeExit(Function&& function)
			: function(std::move(function))
		{
		}

		ScopeExit(const ScopeExit&) = delete;
		ScopeExit& operator=(const ScopeExit&) = delete;

		ScopeExit(ScopeExit&& other) noexcept(noexcept(Function(std::move(other.function))))
			: function(std::move(other.function)), active(other.active)
		{
			other.active = false;
		}

		~ScopeExit() noexcept
		{
			if (active)
				function();
		}

	private:
		Function function;
		bool active = true;
	};

	template<typename Function>
	ScopeExit<Function> operator+(ScopeExitTag, Function&& function)
	{
		return ScopeExit<Function>(std::forward<Function>(function));
	}
}

#define EAPO_CONCAT_IMPL(left, right) left ## right
#define EAPO_CONCAT(left, right) EAPO_CONCAT_IMPL(left, right)
#define EAPO_ANONYMOUS(prefix) EAPO_CONCAT(prefix, __COUNTER__)
#define SCOPE_EXIT auto EAPO_ANONYMOUS(scopeExit) = ::lifetime::ScopeExitTag{} + [&]()
