/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Pure helpers for pairing emitted ASIO clicks with captured WASAPI onsets and
	summarising their latency. Times are in 100 ns units; this header makes no
	Windows calls so the matching policy stays unit-testable.
*/

#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace asiotest
{
	struct LatencyMatch
	{
		std::vector<int64_t> latency100ns;
		size_t matched = 0;
		size_t missing = 0;
		size_t extra = 0;
	};

	// Emissions and onsets are ascending. Each emission takes the first onset
	// still available in its half-open matching window.
	inline LatencyMatch matchClicks(const std::vector<int64_t>& emitted100ns,
		const std::vector<int64_t>& onsets100ns, int64_t window100ns)
	{
		LatencyMatch result;
		result.latency100ns.assign(emitted100ns.size(), INT64_MIN);
		size_t onset = 0;
		for (size_t click = 0; click < emitted100ns.size(); click++)
		{
			const int64_t emitted = emitted100ns[click];
			while (onset < onsets100ns.size() && onsets100ns[onset] < emitted)
				onset++;
			if (window100ns > 0 && onset < onsets100ns.size()
				&& onsets100ns[onset] - emitted >= 0
				&& onsets100ns[onset] - emitted < window100ns)
			{
				result.latency100ns[click] = onsets100ns[onset] - emitted;
				result.matched++;
				onset++;
			}
			else
			{
				result.missing++;
			}
		}
		result.extra = onsets100ns.size() - result.matched;
		return result;
	}

	struct LatencySummary
	{
		size_t count = 0;
		double minUs = 0;
		double medianUs = 0;
		double maxUs = 0;
		double meanUs = 0;
		double stdevUs = 0;
		double firstMinuteMeanUs = 0;
		double lastMinuteMeanUs = 0;
		double slopeUsPerMinute = 0;
		size_t steps = 0;
		double largestStepUs = 0;
	};

	inline LatencySummary summarize(const std::vector<int64_t>& emitted100ns,
		const LatencyMatch& match, double stepThresholdUs)
	{
		LatencySummary result;
		std::vector<double> latencies;
		std::vector<int64_t> emissions;
		const size_t count = (std::min)(emitted100ns.size(), match.latency100ns.size());
		latencies.reserve(match.matched);
		emissions.reserve(match.matched);
		for (size_t i = 0; i < count; i++)
		{
			if (match.latency100ns[i] == INT64_MIN)
				continue;
			latencies.push_back(static_cast<double>(match.latency100ns[i]) / 10.0);
			emissions.push_back(emitted100ns[i]);
		}
		result.count = latencies.size();
		if (latencies.empty())
			return result;

		result.minUs = *std::min_element(latencies.begin(), latencies.end());
		result.maxUs = *std::max_element(latencies.begin(), latencies.end());
		double sum = 0.0;
		for (double latency : latencies)
			sum += latency;
		result.meanUs = sum / static_cast<double>(latencies.size());
		double variance = 0.0;
		for (double latency : latencies)
		{
			const double distance = latency - result.meanUs;
			variance += distance * distance;
		}
		result.stdevUs = std::sqrt(variance / static_cast<double>(latencies.size()));

		std::vector<double> ordered = latencies;
		std::sort(ordered.begin(), ordered.end());
		const size_t middle = ordered.size() / 2;
		result.medianUs = ordered.size() % 2 != 0
			? ordered[middle] : (ordered[middle - 1] + ordered[middle]) / 2.0;

		constexpr int64_t minute100ns = 60LL * 10000000LL;
		double firstSum = 0.0, lastSum = 0.0;
		size_t firstCount = 0, lastCount = 0;
		for (size_t i = 0; i < latencies.size(); i++)
		{
			if (emissions[i] - emissions.front() <= minute100ns)
			{
				firstSum += latencies[i];
				firstCount++;
			}
			if (emissions.back() - emissions[i] <= minute100ns)
			{
				lastSum += latencies[i];
				lastCount++;
			}
		}
		result.firstMinuteMeanUs = firstSum / static_cast<double>(firstCount);
		result.lastMinuteMeanUs = lastSum / static_cast<double>(lastCount);

		if (latencies.size() > 1)
		{
			std::vector<double> minutes(latencies.size());
			double minuteSum = 0.0;
			for (size_t i = 0; i < emissions.size(); i++)
			{
				minutes[i] = static_cast<double>(emissions[i] - emissions.front())
					/ static_cast<double>(minute100ns);
				minuteSum += minutes[i];
			}
			const double minuteMean = minuteSum / static_cast<double>(minutes.size());
			double covariance = 0.0, timeVariance = 0.0;
			for (size_t i = 0; i < latencies.size(); i++)
			{
				const double timeDistance = minutes[i] - minuteMean;
				covariance += timeDistance * (latencies[i] - result.meanUs);
				timeVariance += timeDistance * timeDistance;
			}
			if (timeVariance > 0.0)
				result.slopeUsPerMinute = covariance / timeVariance;

			for (size_t i = 1; i < latencies.size(); i++)
			{
				const double step = latencies[i] - latencies[i - 1];
				if (std::fabs(step) > stepThresholdUs)
				{
					result.steps++;
					if (std::fabs(step) > std::fabs(result.largestStepUs))
						result.largestStepUs = step;
				}
			}
		}
		return result;
	}
}
