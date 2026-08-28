/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

// Process-wide cache that holds *weak* references: each live consumer keeps a
// shared_ptr to its entry, so an entry survives exactly as long as somebody
// uses it, and cache memory stays bounded to the current working set. The
// idiom existed twice, hand-rolled (audit #275 A5/TD-32): the decoded-IR
// cache in filters/IrCache.cpp and the synthesized-IR cache in
// filters/GraphicEQFilter.cpp; both now share this shape and the lifetime
// test in EngineOrchestrationTests covers them together.
//
// Thread-safe. store() prunes expired entries so the map cannot accumulate
// dead keys across config reloads.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class WeakValueCache
{
public:
	std::shared_ptr<Value> find(const Key& key)
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto it = entries.find(key);
		if (it == entries.end())
			return nullptr;
		return it->second.lock();
	}

	void store(const Key& key, const std::shared_ptr<Value>& value)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (auto it = entries.begin(); it != entries.end();)
		{
			if (it->second.expired())
				it = entries.erase(it);
			else
				++it;
		}
		entries[key] = value;
	}

	// Live (lockable) entry count; prunes nothing. For tests.
	size_t liveCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		size_t count = 0;
		for (const auto& entry : entries)
		{
			if (!entry.second.expired())
				count++;
		}
		return count;
	}

private:
	std::mutex mutex;
	std::unordered_map<Key, std::weak_ptr<Value>, Hash> entries;
};
