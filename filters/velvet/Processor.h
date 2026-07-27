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

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace velvet
{
struct Parameters
{
	bool dynamic = true;
	double amount = 1.0;
	double lengthMs = 27.5625;
	double density = 1088.435;
	double refreshSeconds = 5.0;
	double transitionMs = 250.0;
	double decayDb = -60.0;
	std::uint64_t seed = 2050083136ULL;
};

struct Tap
{
	std::uint32_t delay = 0;
	double gain = 0.0;
};

struct Statistics
{
	std::size_t tapsPerChannel = 0;
	double maximumZeroLagCorrelation = 0.0;
	double minimumEnergy = 0.0;
	double maximumEnergy = 0.0;
};

// Shared with the separately distributed MIT VST3 plug-in. Allocation ends in
// prepare(); bank renewal and process() only rewrite preallocated memory.
class Processor
{
public:
	bool prepare(double sampleRate, std::size_t channelCount,
		double maximumLengthMs = 100.0, double maximumDensity = 4000.0);
	bool setParameters(const Parameters& parameters) noexcept;
	void reset() noexcept;

	void process(float* const* output, const float* const* input,
		std::size_t frameCount) noexcept;
	void process(double* const* output, const double* const* input,
		std::size_t frameCount) noexcept;

	bool isPrepared() const noexcept;
	double sampleRate() const noexcept;
	std::size_t channelCount() const noexcept;
	std::size_t tailSamples() const noexcept;
	const Parameters& parameters() const noexcept;
	const std::vector<Tap>& activeTaps(std::size_t channel) const noexcept;
	std::size_t activeTapCount(std::size_t channel) const noexcept;
	Statistics statistics() const noexcept;

private:
	struct Bank
	{
		std::vector<Tap> storage;
		std::size_t count = 0;
	};
	struct Channel
	{
		std::vector<double> delayLine;
		std::size_t writePosition = 0;
		Bank banks[2];
	};

	template <class Sample>
	void processTyped(Sample* const* output, const Sample* const* input,
		std::size_t frameCount) noexcept;
	void generateBank(std::size_t channel, unsigned bankIndex,
		std::uint64_t generation) noexcept;
	static double convolve(const Channel& channel, unsigned bankIndex) noexcept;
	void startTransition() noexcept;
	void finishTransition() noexcept;
	std::size_t secondsToSamples(double seconds) const noexcept;

	double preparedSampleRate = 0.0;
	double maximumLength = 0.0;
	double maximumPulseDensity = 0.0;
	std::size_t maximumLengthSamples = 0;
	std::size_t maximumTapCount = 0;
	std::vector<Channel> channels;
	Parameters current;
	std::size_t currentTailSamples = 0;
	std::size_t refreshSamples = 0;
	std::size_t transitionSamples = 0;
	std::size_t samplesUntilTransition = 0;
	std::size_t transitionPosition = 0;
	std::uint64_t generation = 2;
	unsigned activeBank = 0;
	unsigned pendingBank = 1;
	bool transitioning = false;
};
}
