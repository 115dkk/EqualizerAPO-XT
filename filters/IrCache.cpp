/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

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
#include <cmath>
#include <climits>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ParallelExecutor.h"
#include "helpers/SndfileRAII.h"
#include "IrCache.h"

using std::abs;

namespace
{
	constexpr unsigned kMaxIrChannels = 1024;
	constexpr size_t kParallelDeinterleaveMinimumSamples = 1u << 18;

	// Cache of decoded impulse-response PCM, keyed by path + mtime + sample rate.
	// Lets a config reload (or a second filter using the same IR) skip the
	// libsndfile read + interleave-to-planar pass. File I/O and the per-channel
	// reshuffle dominate convolution filter initialization for large IRs.
	//
	// The cache holds *weak* references: each live filter keeps a shared_ptr to
	// its IrCacheEntry, so the entry survives exactly as long as some filter
	// still uses it. Once the last filter referencing an IR is destroyed the
	// entry is freed, which bounds cache memory to the current config's working
	// set and stops a long-lived process from accumulating every IR it ever
	// loaded.
	struct IrCacheKey
	{
		std::wstring path;
		unsigned long long mtime = 0;
		int sampleRate = 0;

		bool operator==(const IrCacheKey& o) const
		{
			return sampleRate == o.sampleRate && mtime == o.mtime && path == o.path;
		}
	};

	struct IrCacheKeyHash
	{
		size_t operator()(const IrCacheKey& k) const noexcept
		{
			size_t h = std::hash<std::wstring>{}(k.path);
			h ^= std::hash<unsigned long long>{}(k.mtime) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			h ^= std::hash<int>{}(k.sampleRate) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			return h;
		}
	};

	std::mutex& irCacheMutex()
	{
		static std::mutex m;
		return m;
	}

	std::unordered_map<IrCacheKey, std::weak_ptr<const IrCacheEntry>, IrCacheKeyHash>& irCache()
	{
		static std::unordered_map<IrCacheKey, std::weak_ptr<const IrCacheEntry>, IrCacheKeyHash> c;
		return c;
	}

	unsigned long long getMtime(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA attrs;
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs))
			return 0;
		return (static_cast<unsigned long long>(attrs.ftLastWriteTime.dwHighDateTime) << 32)
			| attrs.ftLastWriteTime.dwLowDateTime;
	}
}

std::shared_ptr<const IrCacheEntry> loadIrCached(const std::wstring& filename, double sampleRate)
{
	const int sampleRateKey = static_cast<int>(sampleRate);
	IrCacheKey key{ filename, getMtime(filename), sampleRateKey };

	{
		std::lock_guard<std::mutex> lock(irCacheMutex());
		auto it = irCache().find(key);
		if (it != irCache().end())
		{
			if (auto entry = it->second.lock())
				return entry;
			// Weak reference expired (last filter using it was destroyed);
			// drop the dead slot and fall through to reload.
			irCache().erase(it);
		}
	}

	SF_INFO info{};
	SNDFILE* opened = sf_wchar_open(filename.c_str(), SFM_READ, &info);
	if (opened == nullptr)
	{
		LogFStatic(L"Error while reading impulse response file: %S", sf_strerror(opened));
		return nullptr;
	}
	sndfile::Handle in(opened);
	if (abs(sampleRate - info.samplerate) > 1.0)
	{
		LogFStatic(L"Impulse response sample rate (%d Hz) does not match device sample rate (%f Hz)", info.samplerate, sampleRate);
		return nullptr;
	}

	// Reject impulse responses with no usable audio before they reach the
	// convolution setup. A 0-frame IR makes hcInitSingle dereference an empty
	// filter-pointer array (crash); channels == 0 divides by zero in the
	// channel map; and frames > INT_MAX would wrap negative when cast to int.
	// All three are reachable from a user-writable config plus a crafted IR.
	if (info.frames <= 0 || info.channels <= 0 || info.frames > INT_MAX
		|| static_cast<unsigned>(info.channels) > kMaxIrChannels)
	{
		LogFStatic(L"Impulse response has no usable audio (frames=%lld, channels=%d); ignoring %s",
			static_cast<long long>(info.frames), info.channels, filename.c_str());
		return nullptr;
	}

	const unsigned channels = static_cast<unsigned>(info.channels);
	const unsigned frames = static_cast<unsigned>(info.frames);
	if (static_cast<size_t>(frames) > (std::numeric_limits<size_t>::max)() / channels)
	{
		LogFStatic(L"Impulse response dimensions are too large (frames=%u, channels=%u); ignoring %s",
			frames, channels, filename.c_str());
		return nullptr;
	}
	std::vector<double> interleaved(static_cast<size_t>(frames) * channels);
	sf_count_t numRead = 0;
	while (numRead < info.frames)
	{
		sf_count_t got = sf_readf_double(in.get(), interleaved.data() + numRead * channels, info.frames - numRead);
		if (got <= 0)
			break;
		numRead += got;
	}
	if (numRead != info.frames)
	{
		LogFStatic(L"Impulse response ended early (expected %lld frames, read %lld); ignoring %s",
			static_cast<long long>(info.frames), static_cast<long long>(numRead), filename.c_str());
		return nullptr;
	}

	auto entry = std::make_shared<IrCacheEntry>();
	entry->channels = channels;
	entry->frames = frames;
	entry->buffers.resize(channels);
	auto deinterleaveChannel = [&](size_t index) {
		const unsigned c = static_cast<unsigned>(index);
		entry->buffers[c].resize(frames);
		double* dst = entry->buffers[c].data();
		const double* src = interleaved.data() + c;
		for (unsigned i = 0; i < frames; ++i)
			dst[i] = src[i * channels];
	};
	if (channels > 1 && interleaved.size() >= kParallelDeinterleaveMinimumSamples)
		ParallelExecutor::forEach(channels, deinterleaveChannel);
	else
		for (unsigned c = 0; c < channels; ++c)
			deinterleaveChannel(c);

	{
		std::lock_guard<std::mutex> lock(irCacheMutex());
		// Prune slots whose entries have been freed so the map does not keep
		// accumulating dead keys as IRs come and go across config reloads.
		for (auto it = irCache().begin(); it != irCache().end();)
		{
			if (it->second.expired())
				it = irCache().erase(it);
			else
				++it;
		}
		// store_or_replace: a concurrent loader may have inserted the same key
		// (possibly now expired); overwrite with our live weak reference.
		irCache()[std::move(key)] = entry;
	}
	return entry;
}

void HConvSingleArray::reset()
{
	if (ptr != nullptr)
	{
		for (unsigned i = 0; i < capacity; i++)
			hcCloseSingle(&ptr.get()[i]);

		ptr.reset();
	}
	capacity = 0;
}

HConvSingleArray buildConvolverArray(const std::vector<ConvolverUnitSource>& sources,
	unsigned frameCount)
{
	HConvSingleArray result;
	if (sources.empty())
		return result;

	fftw_make_planner_thread_safe();
	auto allocated = MemoryHelper::allocateArray<HConvSingle>(sources.size());
	if (allocated == nullptr)
	{
		LogFStatic(L"Could not allocate %zu convolution unit(s)", sources.size());
		return result;
	}
	result.adoptStorage(std::move(allocated), static_cast<unsigned>(sources.size()));

	std::vector<unsigned> prototypes;
	prototypes.reserve(sources.size());
	for (unsigned unit = 0; unit < sources.size(); ++unit)
	{
		if (sources[unit].prototype == unit)
			prototypes.push_back(unit);
	}
	ParallelExecutor::forEach(prototypes.size(), [&](size_t index) {
		const unsigned unit = prototypes[index];
		const ConvolverUnitSource& source = sources[unit];
		hcInitSingle(&result[unit], source.samples,
			static_cast<int>(source.sampleCount), static_cast<int>(frameCount), 1);
	});
	for (unsigned unit = 0; unit < sources.size(); ++unit)
	{
		const unsigned prototype = sources[unit].prototype;
		if (prototype != unit)
			hcInitSingleWithSharedFilterBank(&result[unit], &result[prototype]);
	}
	return result;
}
