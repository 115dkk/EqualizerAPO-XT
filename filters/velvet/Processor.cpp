// MIT License
//
// Copyright (c) 2026 115dkk
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "stdafx.h"
#include "Processor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace velvet
{
namespace
{
constexpr double Pi = 3.1415926535897932384626433832795;

std::uint64_t splitMix64(std::uint64_t& state) noexcept
{
	std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

double uniformSigned(std::uint64_t& state) noexcept
{
	const std::uint64_t bits = splitMix64(state) >> 11;
	return static_cast<double>(bits) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
}

std::uint64_t mixedSeed(std::uint64_t seed, std::uint64_t channel,
	std::uint64_t generation) noexcept
{
	std::uint64_t state = seed
		^ (channel + 1) * 0xd6e8feb86659fd93ULL
		^ (generation + 1) * 0xa5a3564e27f8862fULL;
	return splitMix64(state);
}

bool finiteParameters(const Parameters& value) noexcept
{
	return std::isfinite(value.amount) && std::isfinite(value.lengthMs)
		&& std::isfinite(value.density) && std::isfinite(value.refreshSeconds)
		&& std::isfinite(value.transitionMs) && std::isfinite(value.decayDb);
}
}

bool Processor::prepare(double sampleRate, std::size_t channelCount,
	double maximumLengthMs, double maximumDensity)
{
	if (!std::isfinite(sampleRate) || sampleRate < 1000.0 || channelCount == 0
		|| !std::isfinite(maximumLengthMs) || maximumLengthMs < 1.0
		|| !std::isfinite(maximumDensity) || maximumDensity < 1.0)
		return false;

	preparedSampleRate = sampleRate;
	maximumLength = maximumLengthMs;
	maximumPulseDensity = maximumDensity;
	maximumLengthSamples = static_cast<std::size_t>(
		std::ceil(sampleRate * maximumLengthMs / 1000.0)) + 2;
	maximumTapCount = static_cast<std::size_t>(
		std::ceil(maximumLengthMs * maximumDensity / 1000.0)) + 2;

	channels.clear();
	channels.resize(channelCount);
	for (Channel& channel : channels)
	{
		channel.delayLine.assign(maximumLengthSamples, 0.0);
		channel.writePosition = 0;
		for (Bank& bank : channel.banks)
		{
			bank.storage.resize(maximumTapCount);
			bank.count = 0;
		}
	}
	return setParameters(current);
}

bool Processor::setParameters(const Parameters& parameters) noexcept
{
	if (!isPrepared() || !finiteParameters(parameters))
		return false;

	current = parameters;
	current.amount = std::clamp(current.amount, 0.0, 1.0);
	current.lengthMs = std::clamp(current.lengthMs, 1.0, maximumLength);
	current.density = std::clamp(current.density, 1.0, maximumPulseDensity);
	current.refreshSeconds = std::clamp(current.refreshSeconds, 0.1, 60.0);
	current.transitionMs = std::clamp(current.transitionMs, 1.0,
		current.refreshSeconds * 900.0);
	current.decayDb = std::clamp(current.decayDb, -120.0, 0.0);

	currentTailSamples = std::min(maximumLengthSamples - 1,
		static_cast<std::size_t>(std::llround(
			preparedSampleRate * current.lengthMs / 1000.0)));
	currentTailSamples = std::max<std::size_t>(currentTailSamples, 1);
	refreshSamples = std::max<std::size_t>(1,
		secondsToSamples(current.refreshSeconds));
	transitionSamples = std::max<std::size_t>(1,
		secondsToSamples(current.transitionMs / 1000.0));

	activeBank = 0;
	pendingBank = 1;
	transitioning = false;
	transitionPosition = 0;
	samplesUntilTransition = refreshSamples;
	generation = 2;
	for (std::size_t c = 0; c < channels.size(); ++c)
	{
		generateBank(c, 0, 0);
		generateBank(c, 1, 1);
	}
	reset();
	return true;
}

void Processor::reset() noexcept
{
	for (Channel& channel : channels)
	{
		std::fill(channel.delayLine.begin(), channel.delayLine.end(), 0.0);
		channel.writePosition = 0;
	}
	transitioning = false;
	transitionPosition = 0;
	samplesUntilTransition = refreshSamples;
	activeBank = 0;
	pendingBank = 1;
}

void Processor::process(float* const* output, const float* const* input,
	std::size_t frameCount) noexcept
{
	processTyped(output, input, frameCount);
}

void Processor::process(double* const* output, const double* const* input,
	std::size_t frameCount) noexcept
{
	processTyped(output, input, frameCount);
}

template <class Sample>
void Processor::processTyped(Sample* const* output, const Sample* const* input,
	std::size_t frameCount) noexcept
{
	if (!isPrepared() || output == nullptr || input == nullptr)
		return;

	for (std::size_t frame = 0; frame < frameCount; ++frame)
	{
		double oldWeight = 1.0;
		double newWeight = 0.0;
		if (transitioning)
		{
			const double phase = transitionSamples <= 1 ? Pi * 0.5
				: Pi * 0.5 * static_cast<double>(transitionPosition)
					/ static_cast<double>(transitionSamples - 1);
			oldWeight = std::cos(phase);
			newWeight = std::sin(phase);
		}

		for (std::size_t c = 0; c < channels.size(); ++c)
		{
			Channel& channel = channels[c];
			const double dry = input[c] == nullptr ? 0.0
				: static_cast<double>(input[c][frame]);
			channel.delayLine[channel.writePosition] = dry;
			double wet = convolve(channel, activeBank);
			if (transitioning)
				wet = oldWeight * wet + newWeight * convolve(channel, pendingBank);
			if (output[c] != nullptr)
				output[c][frame] = static_cast<Sample>(
					dry + current.amount * (wet - dry));
		}

		for (Channel& channel : channels)
		{
			channel.writePosition++;
			if (channel.writePosition >= channel.delayLine.size())
				channel.writePosition = 0;
		}

		if (!current.dynamic)
			continue;
		if (transitioning)
		{
			transitionPosition++;
			if (transitionPosition >= transitionSamples)
				finishTransition();
		}
		else if (samplesUntilTransition > 0 && --samplesUntilTransition == 0)
		{
			startTransition();
		}
	}
}

void Processor::generateBank(std::size_t channelIndex, unsigned bankIndex,
	std::uint64_t bankGeneration) noexcept
{
	Channel& channel = channels[channelIndex];
	Bank& bank = channel.banks[bankIndex];
	std::size_t count = static_cast<std::size_t>(std::llround(
		current.lengthMs * current.density / 1000.0));
	count = std::clamp<std::size_t>(count, 2,
		std::min(maximumTapCount, currentTailSamples + 1));
	bank.count = count;

	std::uint64_t random = mixedSeed(current.seed, channelIndex, bankGeneration);
	std::uint32_t previous = 0;
	double energy = 0.0;
	const double grid = static_cast<double>(currentTailSamples)
		/ static_cast<double>(count - 1);
	for (std::size_t i = 0; i < count; ++i)
	{
		std::uint32_t delay = 0;
		if (i > 0)
		{
			const double centre = grid * static_cast<double>(i);
			const double jitter = uniformSigned(random) * grid * 0.45;
			const std::uint32_t lower = previous + 1;
			const std::uint32_t upper = static_cast<std::uint32_t>(
				currentTailSamples - (count - 1 - i));
			delay = static_cast<std::uint32_t>(std::clamp<long long>(
				std::llround(centre + jitter), lower, upper));
		}
		previous = delay;
		const double progress = static_cast<double>(delay)
			/ static_cast<double>(currentTailSamples);
		const double envelope = std::pow(10.0,
			current.decayDb * progress / 20.0);
		const double sign = i == 0 || (splitMix64(random) & 1ULL) != 0
			? 1.0 : -1.0;
		bank.storage[i] = {delay, sign * envelope};
		energy += envelope * envelope;
	}
	const double scale = energy > std::numeric_limits<double>::min()
		? 1.0 / std::sqrt(energy) : 1.0;
	for (std::size_t i = 0; i < count; ++i)
		bank.storage[i].gain *= scale;
}

double Processor::convolve(const Channel& channel, unsigned bankIndex) noexcept
{
	const Bank& bank = channel.banks[bankIndex];
	double result = 0.0;
	const std::size_t size = channel.delayLine.size();
	for (std::size_t i = 0; i < bank.count; ++i)
	{
		const Tap& tap = bank.storage[i];
		const std::size_t delay = static_cast<std::size_t>(tap.delay) % size;
		const std::size_t read = channel.writePosition >= delay
			? channel.writePosition - delay
			: channel.writePosition + size - delay;
		result += tap.gain * channel.delayLine[read];
	}
	return result;
}

void Processor::startTransition() noexcept
{
	transitioning = true;
	transitionPosition = 0;
}

void Processor::finishTransition() noexcept
{
	activeBank = pendingBank;
	pendingBank = 1U - activeBank;
	transitioning = false;
	transitionPosition = 0;
	samplesUntilTransition = refreshSamples > transitionSamples
		? refreshSamples - transitionSamples
		: 1;
	for (std::size_t c = 0; c < channels.size(); ++c)
		generateBank(c, pendingBank, generation++);
}

std::size_t Processor::secondsToSamples(double seconds) const noexcept
{
	const double value = preparedSampleRate * seconds;
	if (value <= 1.0)
		return 1;
	if (value >= static_cast<double>((std::numeric_limits<std::size_t>::max)()))
		return (std::numeric_limits<std::size_t>::max)();
	return static_cast<std::size_t>(std::llround(value));
}

bool Processor::isPrepared() const noexcept
{
	return preparedSampleRate > 0.0 && !channels.empty();
}

double Processor::sampleRate() const noexcept { return preparedSampleRate; }
std::size_t Processor::channelCount() const noexcept { return channels.size(); }
std::size_t Processor::tailSamples() const noexcept { return currentTailSamples; }
const Parameters& Processor::parameters() const noexcept { return current; }

const std::vector<Tap>& Processor::activeTaps(std::size_t channel) const noexcept
{
	static const std::vector<Tap> empty;
	return channel < channels.size()
		? channels[channel].banks[activeBank].storage : empty;
}

std::size_t Processor::activeTapCount(std::size_t channel) const noexcept
{
	return channel < channels.size()
		? channels[channel].banks[activeBank].count : 0;
}

Statistics Processor::statistics() const noexcept
{
	Statistics result;
	if (channels.empty())
		return result;
	result.tapsPerChannel = channels.front().banks[activeBank].count;
	result.minimumEnergy = (std::numeric_limits<double>::max)();
	for (const Channel& channel : channels)
	{
		const Bank& bank = channel.banks[activeBank];
		double energy = 0.0;
		for (std::size_t i = 0; i < bank.count; ++i)
			energy += bank.storage[i].gain * bank.storage[i].gain;
		result.minimumEnergy = std::min(result.minimumEnergy, energy);
		result.maximumEnergy = std::max(result.maximumEnergy, energy);
	}
	for (std::size_t a = 0; a < channels.size(); ++a)
	{
		const Bank& left = channels[a].banks[activeBank];
		for (std::size_t b = a + 1; b < channels.size(); ++b)
		{
			const Bank& right = channels[b].banks[activeBank];
			double dot = 0.0;
			std::size_t i = 0;
			std::size_t j = 0;
			while (i < left.count && j < right.count)
			{
				if (left.storage[i].delay == right.storage[j].delay)
				{
					dot += left.storage[i++].gain * right.storage[j++].gain;
				}
				else if (left.storage[i].delay < right.storage[j].delay)
					++i;
				else
					++j;
			}
			result.maximumZeroLagCorrelation = std::max(
				result.maximumZeroLagCorrelation, std::abs(dot));
		}
	}
	return result;
}
}
