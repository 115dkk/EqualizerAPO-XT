/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <mutex>
#include <thread>
#include <utility>

class OwnedBackgroundTask
{
public:
	OwnedBackgroundTask() = default;
	OwnedBackgroundTask(const OwnedBackgroundTask&) = delete;
	OwnedBackgroundTask& operator=(const OwnedBackgroundTask&) = delete;

	~OwnedBackgroundTask()
	{
		join();
	}

	template <typename Function>
	bool startOnce(Function&& function)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (started)
			return false;

		worker = std::thread(std::forward<Function>(function));
		started = true;
		return true;
	}

	void join()
	{
		std::thread ownedWorker;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!worker.joinable())
				return;
			ownedWorker = std::move(worker);
		}
		ownedWorker.join();
	}

private:
	std::mutex mutex;
	std::thread worker;
	bool started = false;
};
