/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026  EqualizerAPO-XT contributors

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#include "stdafx.h"

#include "runtime/concurrency/ParallelExecutor.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	constexpr unsigned kMaximumConcurrency = 16;
}

unsigned ParallelExecutor::concurrencyFor(size_t taskCount, unsigned maxConcurrency) noexcept
{
	if (taskCount == 0)
		return 0;

	unsigned hardwareConcurrency = std::thread::hardware_concurrency();
	if (hardwareConcurrency == 0)
		hardwareConcurrency = 1;

	unsigned requestedConcurrency = maxConcurrency == 0
		? hardwareConcurrency
		: maxConcurrency;
	requestedConcurrency = (std::min)(requestedConcurrency, kMaximumConcurrency);
	const size_t boundedTaskCount = (std::min)(taskCount,
		static_cast<size_t>((std::numeric_limits<unsigned>::max)()));
	return (std::min)(requestedConcurrency, static_cast<unsigned>(boundedTaskCount));
}

void ParallelExecutor::forEach(size_t taskCount, const Operation& operation, unsigned maxConcurrency)
{
	const unsigned concurrency = concurrencyFor(taskCount, maxConcurrency);
	if (concurrency == 0)
		return;
	if (concurrency == 1)
	{
		for (size_t index = 0; index < taskCount; ++index)
			operation(index);
		return;
	}

	std::atomic<size_t> nextIndex{ 0 };
	std::atomic<bool> cancelled{ false };
	std::mutex exceptionMutex;
	std::exception_ptr firstException;

	auto work = [&]() noexcept {
		while (!cancelled.load(std::memory_order_acquire))
		{
			const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
			if (index >= taskCount)
				return;
			try
			{
				operation(index);
			}
			catch (...)
			{
				{
					std::lock_guard<std::mutex> lock(exceptionMutex);
					if (firstException == nullptr)
						firstException = std::current_exception();
				}
				cancelled.store(true, std::memory_order_release);
				return;
			}
		}
	};

	std::vector<std::thread> workers;
	workers.reserve(concurrency - 1);
	try
	{
		for (unsigned i = 1; i < concurrency; ++i)
			workers.emplace_back(work);
	}
	catch (...)
	{
		cancelled.store(true, std::memory_order_release);
		for (std::thread& worker : workers)
			worker.join();
		throw;
	}

	work();
	for (std::thread& worker : workers)
		worker.join();

	if (firstException != nullptr)
		std::rethrow_exception(firstException);
}
