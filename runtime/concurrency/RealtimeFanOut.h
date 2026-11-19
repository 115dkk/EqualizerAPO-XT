/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>

namespace eapo::rt
{
	class FanOut
	{
	public:
		using Task = void (*)(void* context, size_t index) noexcept;

		// Configuration-thread ownership only. No static object owns a lease:
		// the final release stops and joins the team outside DllMain.
		class Lease
		{
		public:
			Lease() noexcept = default;
			~Lease();
			Lease(Lease&& other) noexcept;
			Lease& operator=(Lease&& other) noexcept;
			Lease(const Lease&) = delete;
			Lease& operator=(const Lease&) = delete;
			static Lease acquire();
		private:
			explicit Lease(bool held) noexcept : held(held) {}
			bool held = false;
		};

		// The caller participates. Busy/unavailable teams run inline, without
		// waiting for admission. All tasks have returned when this returns.
		static void run(size_t count, Task fn, void* context) noexcept;
		static unsigned workerCount() noexcept;
		static void setWorkerLimitForTests(unsigned limit) noexcept;
	};
}
