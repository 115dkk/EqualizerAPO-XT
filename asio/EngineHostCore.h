/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The engine host's serving loop, free of any process: one stream on the
	calling thread, from validating the ring to the last block. The
	EqualizerAPOHost.exe runs it on a thread per stream over a named ring;
	the tests and the probe run it on a thread over a heap ring. Two
	FilterEngines per stream, one per direction (capture = true for the
	input lane), built from the header's StreamFormat exactly the way the
	in-process adapter builds them, so the two adapters hash identically.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "runtime/ipc/StreamRing.h"

namespace eapo::asio
{
	struct ServeOptions
	{
		std::wstring configPath;      // empty = registry ConfigPath + watcher
		bool proAudio = true;         // AvSetMmThreadCharacteristics("Pro Audio") for the loop
		uint32_t idleWaitMs = 1000;   // how often an idle loop re-checks the ring state
		// How long the loop polls for the next block before blocking, in
		// buffer periods. One period means the thread never sleeps while a
		// stream runs (a core's worth of CPU for that stream) and never pays
		// a wake-up; zero means it always blocks. The host uses one, the
		// tests zero.
		double spinPeriods = 0.0;
		// Publish the stream's shape under HKCU for the device record. The
		// host does; the tests and the probe leave the user's registry alone.
		bool publishFacts = false;
		// Test hook: when set and true, the loop leaves at once without
		// releasing the block it holds, the way a crashed host would.
		const std::atomic<bool>* abandon = nullptr;
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
	}
}
