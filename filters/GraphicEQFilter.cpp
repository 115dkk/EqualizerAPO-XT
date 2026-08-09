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
#define _USE_MATH_DEFINES
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>

#include "services/logging/Logging.h"
#include "dsp/FftwRAII.h"
#include "runtime/memory/AlignedMemory.h"
#include "GraphicEQFilter.h"

using std::exp;
using std::log;
using std::pow;

namespace
{
	// Cache for the impulse response GraphicEQFilter::initializeFilters synthesizes
	// from the node list. The IR depends only on (nodes, sampleRate, filterLength)
	// and is reused across channels and across config reloads that re-create the
	// same GraphicEQFilter. Building the IR runs an FFT pair plus minimum-phase
	// reconstruction; caching turns that into a vector copy on hit.
	struct EqIrCacheKey
	{
		std::vector<FilterNode> nodes;
		int sampleRate = 0;
		unsigned filterLength = 0;

		bool operator==(const EqIrCacheKey& o) const
		{
			if (sampleRate != o.sampleRate || filterLength != o.filterLength) return false;
			if (nodes.size() != o.nodes.size()) return false;
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				if (nodes[i].freq != o.nodes[i].freq || nodes[i].dbGain != o.nodes[i].dbGain)
					return false;
			}
			return true;
		}
	};

	struct EqIrCacheKeyHash
	{
		size_t operator()(const EqIrCacheKey& k) const noexcept
		{
			size_t h = std::hash<int>{}(k.sampleRate);
			h ^= std::hash<unsigned>{}(k.filterLength) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			for (const auto& n : k.nodes)
			{
				h ^= std::hash<double>{}(n.freq) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
				h ^= std::hash<double>{}(n.dbGain) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
			}
			return h;
		}
	};

	std::mutex& eqIrCacheMutex()
	{
		static std::mutex m;
		return m;
	}

	std::unordered_map<EqIrCacheKey, std::weak_ptr<const std::vector<double>>, EqIrCacheKeyHash>& eqIrCache()
	{
		static std::unordered_map<EqIrCacheKey, std::weak_ptr<const std::vector<double>>, EqIrCacheKeyHash> c;
		return c;
	}
}

GraphicEQFilter::GraphicEQFilter(const std::vector<FilterNode>& nodes, unsigned filterLength)
	: ConvolutionFilter(L""), nodes(nodes), filterLength(filterLength)
{
}

const std::vector<FilterNode>& GraphicEQFilter::getNodes()
{
	return nodes;
}

void GraphicEQFilter::initializeFilters(unsigned frameCount)
{
	EqIrCacheKey key{ nodes, static_cast<int>(sampleRate), filterLength };
	std::shared_ptr<const std::vector<double>> cached;
	{
		std::lock_guard<std::mutex> lock(eqIrCacheMutex());
		auto it = eqIrCache().find(key);
		if (it != eqIrCache().end())
			cached = it->second.lock();
	}

	if (!cached)
	{
		if (filterLength == 0 || filterLength > static_cast<unsigned>((std::numeric_limits<int>::max)() / 2))
			throw std::invalid_argument("GraphicEQ filter length is out of range");

		auto entry = std::make_shared<std::vector<double>>(filterLength);
		const size_t fftLength = static_cast<size_t>(filterLength) * 2;

		fftw_make_planner_thread_safe();
		auto timeData = fftw::allocateComplex(fftLength);
		auto freqData = fftw::allocateComplex(fftLength);
		auto planForward = fftw::makeComplexPlan(static_cast<int>(fftLength), timeData.get(), freqData.get(), FFTW_FORWARD);
		auto planReverse = fftw::makeComplexPlan(static_cast<int>(fftLength), freqData.get(), timeData.get(), FFTW_BACKWARD);

		GainCurveIterator gainIterator(nodes);
		for (unsigned i = 0; i < filterLength; i++)
		{
			double freq = i * 1.0 * sampleRate / (filterLength * 2);
			double dbGain = gainIterator.gainAt(freq);
			double gain = pow(10.0, dbGain / 20.0);

			freqData.get()[i][0] = gain;
			freqData.get()[i][1] = 0;
			freqData.get()[2 * filterLength - i - 1][0] = gain;
			freqData.get()[2 * filterLength - i - 1][1] = 0;
		}

		mps(timeData.get(), freqData.get(), planForward.get(), planReverse.get());

		fftw_execute(planReverse.get());

		for (unsigned i = 0; i < 2 * filterLength; i++)
		{
			timeData.get()[i][0] /= 2 * filterLength;
			timeData.get()[i][1] /= 2 * filterLength;
		}

		for (unsigned i = 0; i < filterLength; i++)
		{
			double factor = 0.5 * (1 + cos(2 * M_PI * i * 1.0 / (2 * filterLength)));
			timeData.get()[i][0] *= factor;
			timeData.get()[i][1] *= factor;
		}

		for (unsigned i = 0; i < filterLength; i++)
			(*entry)[i] = timeData.get()[i][0];

		{
			std::lock_guard<std::mutex> lock(eqIrCacheMutex());
			for (auto it = eqIrCache().begin(); it != eqIrCache().end();)
			{
				if (it->second.expired())
					it = eqIrCache().erase(it);
				else
					++it;
			}
			eqIrCache()[std::move(key)] = entry;
		}
		cached = entry;
	}
	synthesizedIr = cached;
	if (channelCount == 0)
		return;

	// Every channel uses the same synthesized IR. Transform it once, then share
	// the immutable bank while keeping all processing state channel-local.
	std::vector<ConvolverUnitSource> sources(channelCount);
	for (unsigned i = 0; i < channelCount; ++i)
		sources[i] = { cached->data(), filterLength, 0 };
	filters = buildConvolverArray(sources, frameCount);
}

// Minimum phase spectrum from coefficients
void GraphicEQFilter::mps(fftw_complex* timeData, fftw_complex* freqData, fftw_plan planForward, fftw_plan planReverse)
{
	double threshold = pow(10.0, -100.0 / 20.0);
	double logThreshold = log(threshold);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		if (freqData[i][0] < threshold)
			freqData[i][0] = logThreshold;
		else
			freqData[i][0] = log(freqData[i][0]);

		freqData[i][1] = 0;
	}

	fftw_execute(planReverse);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		timeData[i][0] /= filterLength * 2;
		timeData[i][1] /= filterLength * 2;
	}

	for (unsigned i = 1; i < filterLength; i++)
	{
		timeData[i][0] += timeData[filterLength * 2 - i][0];
		timeData[i][1] -= timeData[filterLength * 2 - i][1];

		timeData[filterLength * 2 - i][0] = 0;
		timeData[filterLength * 2 - i][1] = 0;
	}
	timeData[filterLength][1] *= -1;

	fftw_execute(planForward);

	for (unsigned i = 0; i < filterLength * 2; i++)
	{
		double eR = exp(freqData[i][0]);
		freqData[i][0] = double(eR * cos(freqData[i][1]));
		freqData[i][1] = double(eR * sin(freqData[i][1]));
	}
}
