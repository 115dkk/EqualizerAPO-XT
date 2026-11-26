/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "vst/PluginUiThread.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <windows.h>

namespace test
{
// Main-thread runner for console host tests and the hidden-window probe.
class PluginUiThreadRunner : public PluginUiThread::Runner
{
public:
	bool isUiThread() const override { return GetCurrentThreadId() == threadId; }
	bool call(const std::function<void()>& work) override
	{
		std::mutex doneMutex;
		std::condition_variable finished;
		bool done = false;
		if (!post([&] {
			work();
			std::lock_guard<std::mutex> lock(doneMutex);
			done = true;
			finished.notify_one();
		}))
			return false;
		std::unique_lock<std::mutex> lock(doneMutex);
		finished.wait(lock, [&] { return done; });
		return true;
	}
	bool post(std::function<void()> work) override
	{
		std::lock_guard<std::mutex> lock(mutex);
		queued.push_back(std::move(work));
		return true;
	}
	void drain()
	{
		if (draining)
			return;
		draining = true;
		for (;;)
		{
			std::function<void()> work;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (queued.empty())
					break;
				work = std::move(queued.front());
				queued.pop_front();
			}
			work();
		}
		draining = false;
	}
	void wait(HANDLE event)
	{
		while (WaitForSingleObject(event, 0) != WAIT_OBJECT_0)
		{
			drain();
			MSG message;
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			WaitForSingleObject(event, 1);
		}
		drain();
	}
private:
	const DWORD threadId = GetCurrentThreadId();
	std::mutex mutex;
	std::deque<std::function<void()>> queued;
	bool draining = false;
};
}
