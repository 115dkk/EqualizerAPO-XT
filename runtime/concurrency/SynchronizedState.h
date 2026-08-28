/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <mutex>
#include <utility>

template <typename T>
class SynchronizedState
{
public:
	SynchronizedState() = default;
	explicit SynchronizedState(T initialValue)
		: value(std::move(initialValue))
	{
	}

	SynchronizedState(const SynchronizedState&) = delete;
	SynchronizedState& operator=(const SynchronizedState&) = delete;

	template <typename Function>
	decltype(auto) withLock(Function&& function)
	{
		std::lock_guard<std::mutex> lock(mutex);
		return std::forward<Function>(function)(value);
	}

	void replace(T replacement)
	{
		std::lock_guard<std::mutex> lock(mutex);
		value = std::move(replacement);
	}

private:
	std::mutex mutex;
	T value{};
};
