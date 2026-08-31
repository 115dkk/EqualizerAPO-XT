/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/CallbackTrampolines.h"

#include <atomic>
#include <cstdint>

namespace eapo::asio
{
	namespace
	{
		constexpr uint32_t closedBit = 0x80000000u;
		std::atomic<ITargetCallbackSink*> sinks[CallbackTrampolines::slotCount] = {};
		std::atomic<uint32_t> gates[CallbackTrampolines::slotCount] = {};

		bool enter(unsigned slot) noexcept
		{
			const uint32_t previous = gates[slot].fetch_add(1, std::memory_order_acquire);
			if ((previous & closedBit) != 0)
			{
				gates[slot].fetch_sub(1, std::memory_order_release);
				return false;
			}
			return true;
		}

		void leave(unsigned slot) noexcept
		{
			gates[slot].fetch_sub(1, std::memory_order_release);
		}

		template<unsigned slot>
		struct Slot
		{
			static void bufferSwitch(long index, ASIOBool direct)
			{
				if (!enter(slot))
					return;
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					sink->onBufferSwitch(index, direct);
				leave(slot);
			}

			static ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool direct)
			{
				if (!enter(slot))
					return params;
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					params = sink->onBufferSwitchTimeInfo(params, index, direct);
				leave(slot);
				return params;
			}

			static void sampleRateDidChange(ASIOSampleRate rate)
			{
				if (!enter(slot))
					return;
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					sink->onSampleRateDidChange(rate);
				leave(slot);
			}

			static long asioMessage(long selector, long value, void* message, double* opt)
			{
				if (!enter(slot))
					return 0;
				long result = 0;
				if (ITargetCallbackSink* sink = sinks[slot].load(std::memory_order_acquire))
					result = sink->onAsioMessage(selector, value, message, opt);
				leave(slot);
				return result;
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

		int indexOf(const ASIOCallbacks* callbacks) noexcept
		{
			for (unsigned i = 0; i < CallbackTrampolines::slotCount; i++)
			{
				if (callbackSets[i] == callbacks)
					return static_cast<int>(i);
			}
			return -1;
		}
	}

	namespace CallbackTrampolines
	{
		ASIOCallbacks* claim(ITargetCallbackSink* sink) noexcept
		{
			for (unsigned i = 0; i < slotCount; i++)
			{
				if (sinks[i].load(std::memory_order_acquire) != nullptr)
					continue;

				uint32_t expectedGate = closedBit;
				if (!gates[i].compare_exchange_strong(expectedGate, closedBit | 1, std::memory_order_acq_rel))
				{
					expectedGate = 0;
					if (!gates[i].compare_exchange_strong(expectedGate, closedBit | 1, std::memory_order_acq_rel))
						continue;
				}
				ITargetCallbackSink* expectedSink = nullptr;
				if (sinks[i].compare_exchange_strong(expectedSink, sink, std::memory_order_acq_rel))
				{
					gates[i].store(1, std::memory_order_release);
					gates[i].fetch_sub(1, std::memory_order_release);
					return callbackSets[i];
				}
				gates[i].store(closedBit | 1, std::memory_order_release);
				gates[i].fetch_sub(1, std::memory_order_release);
			}
			return nullptr;
		}

		bool release(const ITargetCallbackSink* sink) noexcept
		{
			bool allDrained = true;
			for (unsigned i = 0; i < slotCount; i++)
			{
				if (sinks[i].load(std::memory_order_acquire) == sink)
				{
					const uint32_t previous = gates[i].fetch_or(closedBit, std::memory_order_acq_rel);
					sinks[i].store(nullptr, std::memory_order_release);
					allDrained = allDrained && (previous & ~closedBit) == 0;
				}
			}
			return allDrained;
		}

		bool drained(const ASIOCallbacks* callbacks) noexcept
		{
			const int index = indexOf(callbacks);
			return index >= 0 && (gates[index].load(std::memory_order_acquire) & ~closedBit) == 0;
		}

		bool testEnter(const ASIOCallbacks* callbacks) noexcept
		{
			const int index = indexOf(callbacks);
			return index >= 0 && enter(static_cast<unsigned>(index));
		}

		void testLeave(const ASIOCallbacks* callbacks) noexcept
		{
			const int index = indexOf(callbacks);
			if (index >= 0)
				leave(static_cast<unsigned>(index));
		}
	}
}
