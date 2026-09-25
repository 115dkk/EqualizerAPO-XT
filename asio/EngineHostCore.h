/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The engine host's serving loop, free of any process: one stream on the
	calling thread, from waiting for the producer's header to the last
	block. The EqualizerAPOHost.exe runs it on a thread per stream over a
	named ring; the tests and the probe run it on a thread over a heap ring.
	Both call attachAndServe, so the order the product runs is the one the
	tests exercise. Two FilterEngines per stream, one per direction
	(capture = true for the input lane), built by streamEngineSetup exactly
	the way the in-process adapter builds them, so the two adapters hash
	identically.
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "asio/StreamProcessor.h"
#include "runtime/ipc/StreamRing.h"

class IRegistry;

namespace eapo::asio
{
	struct ServeOptions
	{
		std::wstring configPath;      // empty = registry ConfigPath + watcher
		bool proAudio = true;         // AvSetMmThreadCharacteristics("Pro Audio") for the loop
		uint32_t idleWaitMs = 1000;   // how often an idle loop re-checks the ring state
		// How long attachAndServe waits for the producer to announce the
		// ring. The wrapper waits as long for Ready (a cold start loads
		// config.txt, which may hold convolution IRs); the host has no
		// reason to give up sooner.
		uint32_t readyTimeoutMs = StreamOptions().readyTimeoutMs;
		// How long the loop polls for the next block before blocking, in
		// buffer periods. One period means the thread never sleeps while a
		// stream runs (a core's worth of CPU for that stream) and never pays
		// a wake-up; zero means it always blocks. The host uses one, the
		// tests zero.
		double spinPeriods = 0.0;
		// Where to publish the stream's shape for the device record
		// (asio/StreamFacts.h), or null not to. The host passes the system
		// registry; the tests and the probe leave the user's registry alone
		// or hand in a fake.
		IRegistry* registry = nullptr;
		// When set and true, the loop leaves without releasing the block it
		// holds. It is checked while waiting for the header, between blocks,
		// and after each block is acquired, so a loop waiting for work
		// leaves within idleWaitMs. The host sets it to stop its streams on
		// shutdown; the tests set it to stand in for a crashed host.
		const std::atomic<bool>* abandon = nullptr;
		// Test hook: while set and true, the loop keeps the next block it
		// acquires without processing or releasing it, the way a host stalled
		// by the scheduler would. Clearing it resumes in order.
		const std::atomic<bool>* hold = nullptr;
	};

	struct ServeReport
	{
		uint64_t blocks[2] = {0, 0};
		bool faulted = false;
		bool peerGone = false;
	};

	namespace EngineHostCore
	{
		// Validates the ring, builds the engines, writes Ready (or Fault),
		// then serves until the producer closes or its process handle
		// signals. Never throws: engine failures become Fault.
		ServeReport serveStream(eapo::ipc::RingConsumer& consumer, const ServeOptions& options, uint32_t hostPid) noexcept;

		// The whole attach procedure over a region the caller mapped and the
		// events it opened: waits in 5 ms steps until the producer announces
		// the header, the producer (sync.peer) goes away, abandon turns true
		// or readyTimeoutMs passes; then lets RingConsumer validate what is
		// there and serves it. A header that never left Empty fails
		// validation, which writes Fault and signals ready like any other
		// mismatch. Abandoned before attaching, it leaves the ring untouched.
		ServeReport attachAndServe(void* base, size_t bytes, const eapo::ipc::RingSync& sync, const ServeOptions& options,
			uint32_t hostPid) noexcept;
	}
}
