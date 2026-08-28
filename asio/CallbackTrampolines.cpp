/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/CallbackTrampolines.h"

#include <atomic>

namespace eapo::asio
{
	namespace
	{
		std::atomic<ITargetCallbackSink*> sinks[CallbackTrampolines::slotCount] = {};

		template<unsigned slot>
		struct Slot
		{
			static void bufferSwitch(long index, ASIOBool direct)
			{
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					sink->onBufferSwitch(index, direct);
			}

			static ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool direct)
			{
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					return sink->onBufferSwitchTimeInfo(params, index, direct);
				return params;
			}

			static void sampleRateDidChange(ASIOSampleRate rate)
			{
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					sink->onSampleRateDidChange(rate);
			}

			static long asioMessage(long selector, long value, void* message, double* opt)
			{
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					return sink->onAsioMessage(selector, value, message, opt);
				return 0;
			}

			static ASIOCallbacks callbacks;
		};

		template<unsigned slot>
		ASIOCallbacks Slot<slot>::callbacks = {
			&Slot<slot>::bufferSwitch,
			&Slot<slot>::sampleRateDidChange,
			&Slot<slot>::asioMessage,
			&Slot<slot>::bufferSwitchTimeInfo
		};

		ASIOCallbacks* callbackSets[CallbackTrampolines::slotCount] = {
			&Slot<0>::callbacks, &Slot<1>::callbacks, &Slot<2>::callbacks, &Slot<3>::callbacks
		};
	}

	namespace CallbackTrampolines
	{
		ASIOCallbacks* claim(ITargetCallbackSink* sink) noexcept
		{
			for (unsigned i = 0; i < slotCount; i++)
			{
				ITargetCallbackSink* expected = nullptr;
				if (sinks[i].compare_exchange_strong(expected, sink, std::memory_order_acq_rel))
					return callbackSets[i];
			}
			return nullptr;
		}

		void release(const ITargetCallbackSink* sink) noexcept
		{
			for (unsigned i = 0; i < slotCount; i++)
			{
				if (sinks[i].load(std::memory_order_acquire) == sink)
					sinks[i].store(nullptr, std::memory_order_release);
			}
		}
	}
}
