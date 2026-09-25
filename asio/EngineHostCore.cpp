/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/EngineHostCore.h"

#include <exception>
#include <memory>
#include <vector>

#include <avrt.h>

#include "asio/StreamEngineSetup.h"
#include "asio/StreamFacts.h"
#include "engine/FilterEngine.h"
#include "services/logging/Logging.h"
#include "services/registry/RegistryError.h"

namespace eapo::asio
{
	namespace
	{
		using eapo::ipc::RingConsumer;
		using eapo::ipc::RingFault;
		using eapo::ipc::RingState;

		// MMCSS Pro Audio for the serving thread. Where MMCSS refuses (it is
		// off when SystemResponsiveness is 100), the thread runs time-critical
		// instead and the loop does not spin: a spinning thread outside MMCSS
		// was measured at 95 of 100 probe runs late under load, against 6
		// without the spin.
		struct ProAudioScope
		{
			enum class Mode { Off, On, RefusedTimeCritical };
			HANDLE task = nullptr;
			Mode mode = Mode::Off;
			int previousPriority = THREAD_PRIORITY_NORMAL;

			explicit ProAudioScope(bool enabled)
			{
				if (!enabled)
					return;
				DWORD index = 0;
				task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
				if (task != nullptr)
				{
					AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL);
					mode = Mode::On;
					return;
				}
				previousPriority = GetThreadPriority(GetCurrentThread());
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
				mode = Mode::RefusedTimeCritical;
			}

			~ProAudioScope()
			{
				if (task != nullptr)
					AvRevertMmThreadCharacteristics(task);
				else if (mode == Mode::RefusedTimeCritical)
					SetThreadPriority(GetCurrentThread(), previousPriority);
			}

			const wchar_t* describe() const noexcept
			{
				return mode == Mode::On ? L"on" : mode == Mode::RefusedTimeCritical ? L"refused, time-critical" : L"off";
			}

			ProAudioScope(const ProAudioScope&) = delete;
			ProAudioScope& operator=(const ProAudioScope&) = delete;
		};

		struct Lane
		{
			std::unique_ptr<FilterEngine> engine;
			std::vector<float*> planes;
		};

		// Keeps the serving thread off the processor the producer publishes
		// from. Both threads run at real-time priority; on one core the one
		// that is spinning holds it for a scheduler quantum while the other
		// is ready, which measured as 300-800 us dispatch outliers. Soft:
		// an affinity mask over every other processor of this group, redone
		// only when the producer moves.
		struct CoreAvoidance
		{
			long avoided = -1;
			DWORD_PTR processMask = 0;

			CoreAvoidance()
			{
				DWORD_PTR systemMask = 0;
				GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
			}

			void keepOff(long producerCpu)
			{
				if (producerCpu < 0 || producerCpu == avoided || producerCpu >= static_cast<long>(sizeof(DWORD_PTR) * 8))
					return;
				const DWORD_PTR without = processMask & ~(static_cast<DWORD_PTR>(1) << producerCpu);
				if (without == 0)
					return;      // one usable processor: nothing to keep off
				if (SetThreadAffinityMask(GetCurrentThread(), without) != 0)
					avoided = producerCpu;
			}
		};

		// What the device record reads back for channel counts and rate
		// (asio/StreamFacts.h). Best effort; a stream does not depend on it.
		void publishFacts(IRegistry& registry, const StreamFormat& format) noexcept
		{
			try
			{
				StreamFacts::write(registry, format);
			}
			catch (const RegistryError&)
			{
			}
			catch (...)
			{
			}
		}

		bool buildLane(Lane& lane, const StreamFormat& format, Direction direction, const ServeOptions& options)
		{
			const uint32_t channels = format.channelCount(direction);
			if (channels == 0)
				return true;
			lane.planes.resize(channels);
			lane.engine = std::make_unique<FilterEngine>();
			lane.engine->initialize(streamEngineSetup(format, direction, options.configPath));
			return true;
		}

		bool abandoned(const ServeOptions& options) noexcept
		{
			return options.abandon != nullptr && options.abandon->load();
		}

		// ServeOptions::traceSlowUs: blocks whose wake-up, core move or
		// engine call reached the threshold, or that were acquired after the
		// next block was already published, kept in a fixed table and logged
		// after the loop so the loop itself does no I/O. dispatch counts from
		// the lane's latest publish, which is `behind` blocks later.
		struct SlowTrace
		{
			struct Entry
			{
				unsigned direction;
				uint32_t sequence;
				uint32_t behind;
				uint32_t dispatchUs;
				uint32_t setupUs;
				uint32_t engineUs;
			};
			static constexpr unsigned capacity = 64;
			Entry entries[capacity] = {};
			unsigned count = 0;
			uint64_t dropped = 0;
			double ticksPerMicro = 0.0;

			SlowTrace()
			{
				LARGE_INTEGER frequency;
				QueryPerformanceFrequency(&frequency);
				ticksPerMicro = static_cast<double>(frequency.QuadPart) / 1000000.0;
			}

			uint32_t micros(LONGLONG from, LONGLONG to) const noexcept
			{
				return to > from && ticksPerMicro > 0.0 ? static_cast<uint32_t>(static_cast<double>(to - from) / ticksPerMicro) : 0;
			}

			void note(uint32_t thresholdUs, const RingConsumer::Acquired& acquired, LONGLONG woke, LONGLONG started, LONGLONG done) noexcept
			{
				const uint32_t dispatchUs = micros(acquired.publishTick, woke);
				const uint32_t setupUs = micros(woke, started);
				const uint32_t engineUs = micros(started, done);
				if (acquired.behind == 0 && dispatchUs < thresholdUs && setupUs < thresholdUs && engineUs < thresholdUs)
					return;
				if (count == capacity)
				{
					dropped++;
					return;
				}
				entries[count++] = {static_cast<unsigned>(acquired.direction), acquired.sequence, acquired.behind, dispatchUs, setupUs,
					engineUs};
			}

			void log() const noexcept
			{
				for (unsigned i = 0; i < count; i++)
				{
					const Entry& e = entries[i];
					LogFStatic(L"ASIO host: slow block direction=%s seq=%u behind=%u dispatch=%u us setup=%u us engine=%u us",
						e.direction == static_cast<unsigned>(Direction::Output) ? L"output" : L"input", e.sequence, e.behind, e.dispatchUs,
						e.setupUs, e.engineUs);
				}
				if (dropped != 0)
					LogFStatic(L"ASIO host: %llu more slow blocks not listed", static_cast<unsigned long long>(dropped));
			}
		};

		inline LONGLONG tickNow() noexcept
		{
			LARGE_INTEGER counter;
			QueryPerformanceCounter(&counter);
			return counter.QuadPart;
		}
	}

	namespace EngineHostCore
	{
		ServeReport serveStream(RingConsumer& consumer, const ServeOptions& options, uint32_t hostPid) noexcept
		{
			ServeReport report;
			if (!consumer.valid())
			{
				consumer.setState(RingState::Fault, RingFault::LayoutMismatch);
				report.faulted = true;
				return report;
			}
			consumer.setConsumerPid(hostPid);

			Lane lanes[directionCount];
			const StreamFormat format = consumer.format();
			try
			{
				buildLane(lanes[0], format, Direction::Output, options);
				buildLane(lanes[1], format, Direction::Input, options);
			}
			catch (const std::exception& e)
			{
				LogFStatic(L"ASIO host: engine setup failed for %s: %S", format.deviceName, e.what());
				consumer.setState(RingState::Fault, RingFault::EngineFailed);
				report.faulted = true;
				return report;
			}
			catch (...)
			{
				consumer.setState(RingState::Fault, RingFault::EngineFailed);
				report.faulted = true;
				return report;
			}

			consumer.setState(RingState::Ready);
			if (options.registry != nullptr)
				publishFacts(*options.registry, format);
			ProAudioScope priority(options.proAudio);
			// Whether MMCSS took the thread, for the log a late stream is read from.
			LogFStatic(L"ASIO host: serving %s at %.0f Hz, %u frames, out %u in %u, pro audio %s",
				format.deviceName, format.sampleRate, format.frames, format.channels[0], format.channels[1], priority.describe());
			const uint32_t spinUs = priority.mode == ProAudioScope::Mode::RefusedTimeCritical
				? 0 : static_cast<uint32_t>(options.spinPeriods * periodUs(format));
			CoreAvoidance avoidance;
			std::unique_ptr<SlowTrace> slow = options.traceSlowUs != 0 ? std::make_unique<SlowTrace>() : nullptr;
			RingConsumer::Acquired acquired;
			for (;;)
			{
				if (abandoned(options))
				{
					report.peerGone = false;
					return report;
				}
				if (!consumer.acquire(acquired, options.idleWaitMs, spinUs))
				{
					if (consumer.state() == RingState::Closing || consumer.peerGone())
						break;
					continue;
				}
				const LONGLONG woke = slow != nullptr ? tickNow() : 0;
				avoidance.keepOff(consumer.producerCpu());
				while (options.hold != nullptr && options.hold->load() && !abandoned(options))
					Sleep(1);
				if (abandoned(options))
				{
					report.peerGone = false;
					return report;
				}
				Lane& lane = lanes[static_cast<unsigned>(acquired.direction)];
				const LONGLONG started = slow != nullptr ? tickNow() : 0;
				if (lane.engine != nullptr)
				{
					for (size_t c = 0; c < lane.planes.size(); c++)
						lane.planes[c] = acquired.slot + c * format.frames;
					lane.engine->process(lane.planes.data(), lane.planes.data(), format.frames);
				}
				if (slow != nullptr)
					slow->note(options.traceSlowUs, acquired, woke, started, tickNow());
				report.blocks[static_cast<unsigned>(acquired.direction)]++;
				consumer.release(acquired);
			}
			report.peerGone = consumer.peerGone();
			if (slow != nullptr)
				slow->log();
			LogFStatic(L"ASIO host: stream %s ended (out %llu in %llu blocks%s)", format.deviceName,
				static_cast<unsigned long long>(report.blocks[0]), static_cast<unsigned long long>(report.blocks[1]),
				report.peerGone ? L", producer gone" : L"");
			return report;
		}

		ServeReport attachAndServe(void* base, size_t bytes, const eapo::ipc::RingSync& sync, const ServeOptions& options,
			uint32_t hostPid) noexcept
		{
			ServeReport report;
			if (base == nullptr || bytes < eapo::ipc::ringHeaderBytes)
				return report;
			// The producer formats the header after the host was told about
			// the ring; wait for Announced before validating anything.
			const eapo::ipc::RingHeader* header = static_cast<const eapo::ipc::RingHeader*>(base);
			const ULONGLONG deadline = GetTickCount64() + options.readyTimeoutMs;
			while (ReadAcquire(&header->state) == static_cast<LONG>(RingState::Empty) && !abandoned(options))
			{
				if (sync.peer != nullptr)
				{
					const DWORD waited = WaitForSingleObject(sync.peer, 5);
					if (waited == WAIT_OBJECT_0)
						break;
					if (waited == WAIT_FAILED)
						Sleep(5);
				}
				else
				{
					Sleep(5);
				}
				if (GetTickCount64() >= deadline)
					break;
			}
			if (abandoned(options))
				return report;
			// RingConsumer checks everything a header can get wrong, an Empty
			// one included; serveStream turns a refusal into Fault.
			RingConsumer consumer(base, bytes, sync);
			return serveStream(consumer, options, hostPid);
		}
	}
}
