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
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <climits>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>
#include <fftw3.h>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "ConvolutionFilter.h"
#include "helpers/PerfProfile.h"

using std::abs;
using std::vector;
using std::wstring;

// Decoded impulse-response PCM, shared between ConvolutionFilter instances that
// reference the same IR. Defined at global scope (not in the anonymous namespace)
// so ConvolutionFilter can forward-declare it and hold a std::shared_ptr member.
struct IrCacheEntry
{
	unsigned channels = 0;
	unsigned frames = 0;
	// Channel-major IR samples; each inner vector has `frames` elements.
	std::vector<std::vector<double>> buffers;
};

namespace
{
	// Cache of decoded impulse-response PCM, keyed by path + mtime + sample rate.
	// Lets a config reload (or a second ConvolutionFilter using the same IR) skip
	// the libsndfile read + interleave-to-planar pass. File I/O and the
	// per-channel reshuffle dominate ConvolutionFilter initialization for large
	// IRs.
	//
	// The cache holds *weak* references: each live ConvolutionFilter keeps a
	// shared_ptr to its IrCacheEntry, so the entry survives exactly as long as some
	// filter still uses it. Once the last filter referencing an IR is destroyed the
	// entry is freed, which bounds cache memory to the current config's working set
	// and stops a long-lived process from accumulating every IR it ever loaded.
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
		SNDFILE* in = sf_wchar_open(filename.c_str(), SFM_READ, &info);
		if (in == nullptr)
		{
			LogFStatic(L"Error while reading impulse response file: %S", sf_strerror(in));
			return nullptr;
		}
		if (abs(sampleRate - info.samplerate) > 1.0)
		{
			LogFStatic(L"Impulse response sample rate (%d Hz) does not match device sample rate (%f Hz)", info.samplerate, sampleRate);
			sf_close(in);
			return nullptr;
		}

		// Reject impulse responses with no usable audio before they reach the
		// convolution setup. A 0-frame IR makes hcInitSingle dereference an empty
		// filter-pointer array (crash); channels == 0 divides by zero in the
		// channel map; and frames > INT_MAX would wrap negative when cast to int.
		// All three are reachable from a user-writable config plus a crafted IR.
		if (info.frames <= 0 || info.channels <= 0 || info.frames > INT_MAX)
		{
			LogFStatic(L"Impulse response has no usable audio (frames=%lld, channels=%d); ignoring %s",
				static_cast<long long>(info.frames), info.channels, filename.c_str());
			sf_close(in);
			return nullptr;
		}

		const unsigned channels = static_cast<unsigned>(info.channels);
		const unsigned frames = static_cast<unsigned>(info.frames);
		std::vector<double> interleaved(static_cast<size_t>(frames) * channels);
		sf_count_t numRead = 0;
		while (numRead < info.frames)
		{
			sf_count_t got = sf_readf_double(in, interleaved.data() + numRead * channels, info.frames - numRead);
			if (got <= 0)
				break; // truncated or unreadable file: stop instead of spinning forever
			numRead += got;
		}
		sf_close(in);

		auto entry = std::make_shared<IrCacheEntry>();
		entry->channels = channels;
		entry->frames = frames;
		entry->buffers.resize(channels);
		for (unsigned c = 0; c < channels; ++c)
		{
			entry->buffers[c].resize(frames);
			double* dst = entry->buffers[c].data();
			const double* src = interleaved.data() + c;
			for (unsigned i = 0; i < frames; ++i)
				dst[i] = src[i * channels];
		}

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

	// Running total of process() calls that took the frameCount-mismatch mute path,
	// across all ConvolutionFilter instances. The mute is otherwise silent after the
	// first log, so this makes the condition observable (logged in cleanup()). The
	// mute path can run on the audio thread, hence atomic.
	std::atomic<unsigned long long> muteCallCount{ 0 };
}

void HConvSingleArray::reset()
{
	if (ptr != nullptr)
	{
		for (unsigned i = 0; i < channelCount; i++)
			hcCloseSingle(&ptr[i]);

		MemoryHelper::free(ptr);
		ptr = nullptr;
	}
}

ConvolutionFilter::ConvolutionFilter(const wstring& filename)
	: filters(channelCount)
{
	this->filename = filename;
	filterFrameCount = 0;
	maxFrameCount = 0;
	frameCountMismatchLogged = false;
}

ConvolutionFilter::~ConvolutionFilter()
{
	cleanup();
}

vector<wstring> ConvolutionFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	this->sampleRate = sampleRate;
	this->maxFrameCount = maxFrameCount;
	channelCount = (unsigned)channelNames.size();
	filterFrameCount = 0;

	initializeFilters(maxFrameCount);
	if (filters != nullptr)
		filterFrameCount = maxFrameCount;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void ConvolutionFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("ConvolutionFilter::process");
	if (filters == nullptr)
		return;
	if (frameCount == 0)
		return;

	// libHybridConv는 hcInitSingle 시점의 framelength로 고정 처리한다.
	// 과거에는 process() 안에서 cleanup() + initializeFilters()를 호출해 audio 콜백 중에 파일 I/O,
	// FFTW plan, malloc/free가 발생했다. RT 안정성을 위해 재초기화를 금지하고, mismatch가 들어오면
	// 한 번만 로그를 남긴 뒤 무음으로 빠진다. 정상 stream에서는 LockForProcess가 frameCount를 고정하므로
	// 이 분기는 거의 들어오지 않는다.
	if (frameCount != filterFrameCount)
	{
		const unsigned long long count = muteCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (!frameCountMismatchLogged)
		{
			LogF(L"ConvolutionFilter: frameCount %u differs from initialized %u; output muted (audio-thread re-init skipped) [mute calls so far: %llu]", frameCount, filterFrameCount, count);
			frameCountMismatchLogged = true;
		}
		for (unsigned i = 0; i < channelCount; i++)
			memset(output[i], 0, sizeof(double) * frameCount);
		return;
	}

	for (unsigned i = 0; i < channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		HConvSingle* filter = &filters[i];

		hcPutSingle(filter, inputChannel);
		hcProcessSingle(filter);
		hcGetSingle(filter, outputChannel);
	}
}
#pragma AVRT_CODE_END

void ConvolutionFilter::cleanup()
{
	// HConvSingleArray::reset() runs the exact close-then-free sequence; assigning
	// nullptr makes the teardown automatic and idempotent.
	filters = nullptr;
	// Release this filter's hold on the cached IR. With the cache holding only weak
	// references, dropping the last shared_ptr frees the entry.
	irEntry.reset();
	filterFrameCount = 0;
	frameCountMismatchLogged = false;

	// Surface how often the mismatch mute fired (silent after the first log).
	if (muteCallCount.load(std::memory_order_relaxed) != 0)
		LogF(L"ConvolutionFilter: frameCount-mismatch mute fired %llu time(s) total", muteCallCount.load(std::memory_order_relaxed));
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	auto ir = loadIrCached(filename, sampleRate);
	if (!ir)
		return;

	// Pin the cached IR for this filter's lifetime. The process-wide cache keeps
	// only a weak reference, so this member is what keeps the entry resident while
	// the filter exists; cleanup() releases it.
	irEntry = ir;

	TraceF(L"Convolving using impulse response file %s (%u channels, %u frames)",
		filename.c_str(), ir->channels, ir->frames);

	fftw_make_planner_thread_safe();
	HConvSingle* allocated = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
	if (allocated == nullptr)
	{
		// alloc returns nullptr on failure; bail to the inert state (filters stays
		// null from cleanup(), process() then no-ops) rather than dereferencing it.
		LogF(L"ConvolutionFilter: could not allocate %u filter slots", channelCount);
		return;
	}
	filters = allocated;
	for (unsigned i = 0; i < channelCount; i++)
	{
		// hcInitSingle reads the IR samples during planning but does not retain
		// the pointer, so it is safe to feed it the shared cache buffer.
		const std::vector<double>& src = ir->buffers[i % ir->channels];
		hcInitSingle(&filters[i], const_cast<double*>(src.data()), static_cast<int>(ir->frames), static_cast<int>(frameCount), 1);
	}
}
