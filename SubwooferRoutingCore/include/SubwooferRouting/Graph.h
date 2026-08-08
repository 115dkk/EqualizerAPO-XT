// SPDX-License-Identifier: MIT

#pragma once

#include "Headroom.h"
#include "State.h"

#include <cstddef>
#include <string>
#include <vector>

namespace subroute
{

struct PrepareSpec
{
	double sampleRate = 48000.0;
	std::size_t maximumBlockSize = 1024;

	/*
		Ordered canonical physical channel IDs. Each position is one physical
		input/output slot. Additional device channels not referenced by the
		state remain available for bit-exact pass-through.
	*/
	std::vector<std::string> channelLayout;
};

/*
	Coefficients use normalized a0 = 1 and the difference equation:

		y[n] =
			b0*x[n] + b1*x[n-1] + b2*x[n-2]
			- a1*y[n-1] - a2*y[n-2]
*/
struct BiquadCoefficients
{
	double b0 = 1.0;
	double b1 = 0.0;
	double b2 = 0.0;
	double a1 = 0.0;
	double a2 = 0.0;
};

struct CompiledSourceTerm
{
	std::size_t inputChannelIndex = 0;
	double gainLinear = 1.0;
};

enum class CompiledStageKind
{
	Gain,
	Delay,
	Biquad
};

/*
	A compiled delay D is represented as:

		D = integerDelaySamples + fractionalDelaySamples

	where fractionalDelaySamples is in [0, 1). Processing uses causal linear
	interpolation:

		y[n] =
			(1 - fractionalDelaySamples) * x[n - integerDelaySamples]
			+ fractionalDelaySamples * x[n - integerDelaySamples - 1]
*/
struct CompiledStage
{
	CompiledStageKind kind = CompiledStageKind::Gain;
	double gainLinear = 1.0;
	std::size_t integerDelaySamples = 0;
	double fractionalDelaySamples = 0.0;
	BiquadCoefficients biquad;
};

struct CompiledPath
{
	std::string id;
	PathKind kind = PathKind::Main;
	std::vector<CompiledSourceTerm> sourceMix;
	std::vector<CompiledStage> stages;
};

struct CompiledOutputTerm
{
	std::size_t sourcePathIndex = 0;
	double gainLinear = 1.0;
};

struct CompiledOutput
{
	std::string targetChannelId;
	std::size_t targetChannelIndex = 0;
	OutputMode mode = OutputMode::Replace;
	std::vector<CompiledOutputTerm> terms;
};

class ProcessingGraph
{
public:
	ProcessingGraph(
		PrepareSpec prepareSpec,
		std::vector<CompiledPath> paths,
		std::vector<CompiledOutput> outputs,
		HeadroomAnalysis headroom);

	ProcessingGraph(const ProcessingGraph&) = default;
	ProcessingGraph(ProcessingGraph&&) noexcept = default;
	ProcessingGraph& operator=(const ProcessingGraph&) = default;
	ProcessingGraph& operator=(ProcessingGraph&&) noexcept = default;
	~ProcessingGraph() = default;

	const PrepareSpec& prepareSpec() const noexcept;
	const std::vector<CompiledPath>& paths() const noexcept;
	const std::vector<CompiledOutput>& outputs() const noexcept;
	const HeadroomAnalysis& headroom() const noexcept;

private:
	PrepareSpec prepareSpec_;
	std::vector<CompiledPath> paths_;
	std::vector<CompiledOutput> outputs_;
	HeadroomAnalysis headroom_;
};

}
