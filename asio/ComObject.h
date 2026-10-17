/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <atomic>

namespace eapo::asio
{
	template<class Self, class Interface>
	class ComObject : public Interface
	{
	public:
		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(refCount_.fetch_add(1, std::memory_order_acq_rel) + 1);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const long remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
			if (remaining == 0)
			{
				delete static_cast<Self*>(this);
				return 0;
			}
			return static_cast<ULONG>(remaining);
		}

	protected:
		ComObject() = default;
		~ComObject() = default;

	private:
		std::atomic<long> refCount_{1};
	};
}
