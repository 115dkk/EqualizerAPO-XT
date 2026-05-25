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
#include <memory>
#include <mutex>
#include <unordered_map>
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

namespace
{
	// Cache of decoded impulse-response PCM, keyed by path + mtime + sample rate.
	// Lets a config reload (or a second ConvolutionFilter using the same IR) skip
	// the libsndfile read + interleave-to-planar pass. File I/O and the
	// per-channel reshuffle dominate ConvolutionFilter initialization for large
	// IRs; we keep the cache process-wide and unbounded since IRs rarely change
	// inside a session and each entry is at most the file's PCM size.
	struct IrCacheKey
	{
		std::wstring path;
		unsigned long long mtime;
		int sampleRate;

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

	struct IrCacheEntry
	{
		unsigned channels = 0;
		unsigned frames = 0;
		// Channel-major IR samples; each inner vector has `frames` elements.
		std::vector<std::vector<double>> buffers;
	};

	std::mutex& irCacheMutex()
	{
		static std::mutex m;
		return m;
	}

	std::unordered_map<IrCacheKey, std::shared_ptr<const IrCacheEntry>, IrCacheKeyHash>& irCache()
	{
		static std::unordered_map<IrCacheKey, std::shared_ptr<const IrCacheEntry>, IrCacheKeyHash> c;
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
				return it->second;
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

		const unsigned channels = static_cast<unsigned>(info.channels);
		const unsigned frames = static_cast<unsigned>(info.frames);
		std::vector<double> interleaved(static_cast<size_t>(frames) * channels);
		sf_count_t numRead = 0;
		while (numRead < info.frames)
			numRead += sf_readf_double(in, interleaved.data() + numRead * channels, info.frames - numRead);
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
			irCache().emplace(std::move(key), entry);
		}
		return entry;
	}
}

ConvolutionFilter::ConvolutionFilter(wstring filename)
{
	this->filename = filename;
	filters = nullptr;
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
		if (!frameCountMismatchLogged)
		{
			LogF(L"ConvolutionFilter: frameCount %u differs from initialized %u; output muted (audio-thread re-init skipped)", frameCount, filterFrameCount);
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
	if (filters != nullptr)
	{
		for (unsigned i = 0; i < channelCount; i++)
			hcCloseSingle(&filters[i]);

		MemoryHelper::free(filters);
		filters = nullptr;
	}
	filterFrameCount = 0;
	frameCountMismatchLogged = false;
}

void ConvolutionFilter::initializeFilters(unsigned frameCount)
{
	auto ir = loadIrCached(filename, sampleRate);
	if (!ir)
		return;

	TraceF(L"Convolving using impulse response file %s (%u channels, %u frames)",
		filename.c_str(), ir->channels, ir->frames);

	fftw_make_planner_thread_safe();
	filters = (HConvSingle*)MemoryHelper::alloc(sizeof(HConvSingle) * channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		// hcInitSingle reads the IR samples during planning but does not retain
		// the pointer, so it is safe to feed it the shared cache buffer.
		const std::vector<double>& src = ir->buffers[i % ir->channels];
		hcInitSingle(&filters[i], const_cast<double*>(src.data()), static_cast<int>(ir->frames), static_cast<int>(frameCount), 1);
	}
}
