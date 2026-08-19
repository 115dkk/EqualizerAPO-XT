/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <exception>

#include "services/logging/Logging.h"
#include "ConfigWatcher.h"
#include "FilterEngine.h"

// This TU is the configuration-watch thread glue and nothing else. The
// load-time graph construction (addFilters) lives with the rest of the
// loading code in FilterEngine.Configuration.cpp, and the realtime
// transition bookkeeping (finishTransitionIfReady) lives with the
// processing hot path in FilterEngine.Process.cpp (audit #275 A6).

using std::exception;
using std::lock_guard;
using std::mutex;

void FilterEngine::notificationThread(FilterEngine* engine)
{
	// Audit #250 F030: this is a std::thread body inside audiodg. An
	// exception escaping it (Win32Event construction, allocation in the
	// snapshot lambda) would reach std::terminate and take the audio
	// engine down with it. Watching stops, but the stream must survive.
	try
	{
		ConfigWatcher watcher(
			engine->configChannel.shutdownHandle(),
			[engine] {
				ConfigWatcher::Snapshot snapshot;
				lock_guard<mutex> lock(engine->loadMutex);
				snapshot.directory = engine->configPath;
				snapshot.registryKeys.assign(
					engine->load.watchRegistryKeys.begin(),
					engine->load.watchRegistryKeys.end());
				return snapshot;
			},
			[engine] {
				if (!engine->configChannel.acquirePublishPermit())
					return false;

				const bool loaded = engine->loadConfig();
				if (!loaded)
					engine->configChannel.releasePublishPermit();
				return true;
			});
		watcher.run();
	}
	catch (const exception& e)
	{
		LogFStatic(L"Configuration watcher stopped by exception: %S", e.what());
	}
	catch (...)
	{
		LogFStatic(L"Configuration watcher stopped by an unknown exception");
	}
}
