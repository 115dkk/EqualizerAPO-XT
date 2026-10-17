/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Runs blocking system queries (the endpoint format probes, which go
	through COM and the audio service and can stall on a slow or unplugged
	device) on one background thread and hands each result back on the UI
	thread, so the window stays responsive meanwhile (audit #422 TD-04). The
	shape follows PluginLoadQueue: completions are posted to a receiver owned
	by the running QCoreApplication, under the queue's lock.
*/

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

class QObject;

namespace BackgroundProbe
{
// Runs work on the probe thread (COM initialized there for the
// multithreaded apartment), then done on the UI thread unless context was
// destroyed meanwhile. Work runs in request order. Call on the UI thread
// while a QCoreApplication exists.
void run(QObject* context, std::function<void()> work, std::function<void()> done);

// The same for work that produces a value: done receives it.
template<class Work, class Done>
void runFor(QObject* context, Work work, Done done)
{
	using Result = std::invoke_result_t<Work&>;
	auto result = std::make_shared<Result>();
	run(context,
		[result, work = std::move(work)]() mutable { *result = work(); },
		[result, done = std::move(done)]() mutable { done(*result); });
}

// Drops queued work, waits for the item in progress and ends the probe
// thread. main calls it once the last QApplication is gone, beside
// PluginLoadQueue::shutdown.
void shutdown();
}
