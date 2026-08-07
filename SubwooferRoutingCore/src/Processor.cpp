// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace subroute
{

AudioBlock::AudioBlock(
	const float* const* inputs,
	float* const* outputs,
	std::size_t channelCount,
	std::size_t frameCount) noexcept
	: sampleFormat_(SampleFormat::Float32),
	  channelCount_(channelCount),
	  frameCount_(frameCount),
	  floatInputs_(inputs),
	  floatOutputs_(outputs),
	  doubleInputs_(nullptr),
	  doubleOutputs_(nullptr)
{
}

AudioBlock::AudioBlock(
	const double* const* inputs,
	double* const* outputs,
	std::size_t channelCount,
	std::size_t frameCount) noexcept
	: sampleFormat_(SampleFormat::Float64),
	  channelCount_(channelCount),
	  frameCount_(frameCount),
	  floatInputs_(nullptr),
	  floatOutputs_(nullptr),
	  doubleInputs_(inputs),
	  doubleOutputs_(outputs)
{
}

SampleFormat AudioBlock::sampleFormat() const noexcept
{
	return sampleFormat_;
}

std::size_t AudioBlock::channelCount() const noexcept
{
	return channelCount_;
}

std::size_t AudioBlock::frameCount() const noexcept
{
	return frameCount_;
}

const float* const* AudioBlock::floatInputs() const noexcept
{
	return floatInputs_;
}

float* const* AudioBlock::floatOutputs() const noexcept
{
	return floatOutputs_;
}

const double* const* AudioBlock::doubleInputs() const noexcept
{
	return doubleInputs_;
}

double* const* AudioBlock::doubleOutputs() const noexcept
{
	return doubleOutputs_;
}

class Processor::Impl
{
public:
	struct RuntimeStage
	{
		std::vector<double> delayLine;
		std::size_t delayWriteIndex = 0;
		double biquadState1 = 0.0;
		double biquadState2 = 0.0;
	};

	struct RuntimePath
	{
		std::vector<double> scratch;
		std::vector<RuntimeStage> stages;
	};

	PrepareSpec prepareSpec;
	std::vector<CompiledPath> paths;
	std::vector<CompiledOutput> outputs;
	HeadroomAnalysis headroom;
	std::vector<RuntimePath> runtimePaths;
	std::vector<double> preservedInputs;
	std::vector<float> floatConversionPlanes;
	std::vector<unsigned char> targetedChannels;
	bool prepared = false;

	void reset() noexcept
	{
		std::fill(preservedInputs.begin(), preservedInputs.end(), 0.0);
		std::fill(floatConversionPlanes.begin(), floatConversionPlanes.end(), 0.0f);

		for (RuntimePath& path : runtimePaths)
		{
			std::fill(path.scratch.begin(), path.scratch.end(), 0.0);

			for (RuntimeStage& stage : path.stages)
			{
				std::fill(stage.delayLine.begin(), stage.delayLine.end(), 0.0);
				stage.delayWriteIndex = 0;
				stage.biquadState1 = 0.0;
				stage.biquadState2 = 0.0;
			}
		}
	}

	void process(const AudioBlock& block) noexcept
	{
		if (!prepared)
		{
			return;
		}

		const std::size_t channelCount = prepareSpec.channelLayout.size();
		const std::size_t frameCount = block.frameCount();

		if (block.channelCount() != channelCount ||
			frameCount > prepareSpec.maximumBlockSize)
		{
			return;
		}

		const bool isFloat = block.sampleFormat() == SampleFormat::Float32;

		if (isFloat)
		{
			const float* const* inputs = block.floatInputs();
			float* const* outputsForValidation = block.floatOutputs();

			if (inputs == nullptr || outputsForValidation == nullptr)
			{
				return;
			}

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				if (inputs[channel] == nullptr ||
					outputsForValidation[channel] == nullptr)
				{
					return;
				}
			}
		}
		else
		{
			const double* const* inputs = block.doubleInputs();
			double* const* outputsForValidation = block.doubleOutputs();

			if (inputs == nullptr || outputsForValidation == nullptr)
			{
				return;
			}

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				if (inputs[channel] == nullptr ||
					outputsForValidation[channel] == nullptr)
				{
					return;
				}
			}
		}

		if (isFloat)
		{
			const float* const* inputs = block.floatInputs();

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				const std::size_t offset =
					channel * prepareSpec.maximumBlockSize;
				float* const nativePreserved =
					floatConversionPlanes.data() + offset;
				double* const preserved = preservedInputs.data() + offset;

				std::memcpy(
					nativePreserved,
					inputs[channel],
					frameCount * sizeof(float));

				for (std::size_t frame = 0; frame < frameCount; ++frame)
				{
					preserved[frame] =
						static_cast<double>(nativePreserved[frame]);
				}
			}
		}
		else
		{
			const double* const* inputs = block.doubleInputs();

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				const std::size_t offset =
					channel * prepareSpec.maximumBlockSize;

				std::memcpy(
					preservedInputs.data() + offset,
					inputs[channel],
					frameCount * sizeof(double));
			}
		}

		for (std::size_t pathIndex = 0;
			pathIndex < paths.size();
			++pathIndex)
		{
			const CompiledPath& path = paths[pathIndex];
			RuntimePath& runtimePath = runtimePaths[pathIndex];
			double* const scratch = runtimePath.scratch.data();

			std::fill(scratch, scratch + frameCount, 0.0);

			for (const CompiledSourceTerm& source : path.sourceMix)
			{
				const double* const preserved =
					preservedInputs.data() +
					source.inputChannelIndex *
						prepareSpec.maximumBlockSize;

				for (std::size_t frame = 0; frame < frameCount; ++frame)
				{
					scratch[frame] +=
						preserved[frame] * source.gainLinear;
				}
			}

			for (std::size_t stageIndex = 0;
				stageIndex < path.stages.size();
				++stageIndex)
			{
				const CompiledStage& stage = path.stages[stageIndex];
				RuntimeStage& runtimeStage =
					runtimePath.stages[stageIndex];

				switch (stage.kind)
				{
				case CompiledStageKind::Gain:
					for (std::size_t frame = 0;
						frame < frameCount;
						++frame)
					{
						scratch[frame] *= stage.gainLinear;
					}
					break;

				case CompiledStageKind::Delay:
				{
					double* const delayLine =
						runtimeStage.delayLine.data();
					const std::size_t delayLineSize =
						runtimeStage.delayLine.size();
					const std::size_t integerDelay =
						stage.integerDelaySamples;
					const double fractionalDelay =
						stage.fractionalDelaySamples;
					const double currentWeight =
						1.0 - fractionalDelay;

					for (std::size_t frame = 0;
						frame < frameCount;
						++frame)
					{
						const std::size_t writeIndex =
							runtimeStage.delayWriteIndex;
						delayLine[writeIndex] = scratch[frame];

						const std::size_t currentIndex =
							writeIndex >= integerDelay
								? writeIndex - integerDelay
								: writeIndex +
									delayLineSize -
									integerDelay;
						const std::size_t previousOffset =
							integerDelay + 1;
						const std::size_t previousIndex =
							writeIndex >= previousOffset
								? writeIndex - previousOffset
								: writeIndex +
									delayLineSize -
									previousOffset;

						scratch[frame] =
							currentWeight * delayLine[currentIndex] +
							fractionalDelay * delayLine[previousIndex];

						runtimeStage.delayWriteIndex =
							writeIndex + 1 == delayLineSize
								? 0
								: writeIndex + 1;
					}
				}
					break;

				case CompiledStageKind::Biquad:
				{
					const BiquadCoefficients& coefficients =
						stage.biquad;
					double state1 = runtimeStage.biquadState1;
					double state2 = runtimeStage.biquadState2;

					// DF2T retains only the two states required by each section.
					for (std::size_t frame = 0;
						frame < frameCount;
						++frame)
					{
						const double input = scratch[frame];
						const double output =
							coefficients.b0 * input + state1;

						state1 =
							coefficients.b1 * input -
							coefficients.a1 * output +
							state2;
						state2 =
							coefficients.b2 * input -
							coefficients.a2 * output;

						scratch[frame] = output;
					}

					runtimeStage.biquadState1 = state1;
					runtimeStage.biquadState2 = state2;
				}
					break;
				}
			}
		}

		// Pass-through copies happen before matrix writes to tolerate cross-input aliases.
		if (isFloat)
		{
			const float* const* inputs = block.floatInputs();
			float* const* nativeOutputs = block.floatOutputs();

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				if (targetedChannels[channel] != 0 ||
					nativeOutputs[channel] == inputs[channel])
				{
					continue;
				}

				const std::size_t offset =
					channel * prepareSpec.maximumBlockSize;

				std::memcpy(
					nativeOutputs[channel],
					floatConversionPlanes.data() + offset,
					frameCount * sizeof(float));
			}
		}
		else
		{
			const double* const* inputs = block.doubleInputs();
			double* const* nativeOutputs = block.doubleOutputs();

			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				if (targetedChannels[channel] != 0 ||
					nativeOutputs[channel] == inputs[channel])
				{
					continue;
				}

				const std::size_t offset =
					channel * prepareSpec.maximumBlockSize;

				std::memcpy(
					nativeOutputs[channel],
					preservedInputs.data() + offset,
					frameCount * sizeof(double));
			}
		}

		for (const CompiledOutput& output : outputs)
		{
			const std::size_t targetOffset =
				output.targetChannelIndex *
				prepareSpec.maximumBlockSize;
			const double* const originalTarget =
				preservedInputs.data() + targetOffset;

			if (isFloat)
			{
				float* const converted =
					floatConversionPlanes.data() + targetOffset;

				for (std::size_t frame = 0; frame < frameCount; ++frame)
				{
					double value =
						output.mode == OutputMode::Add
							? originalTarget[frame]
							: 0.0;

					for (const CompiledOutputTerm& term : output.terms)
					{
						value +=
							runtimePaths[term.sourcePathIndex]
								.scratch[frame] *
							term.gainLinear;
					}

					converted[frame] = static_cast<float>(
						value * headroom.appliedTrimLinear);
				}

				std::memcpy(
					block.floatOutputs()[output.targetChannelIndex],
					converted,
					frameCount * sizeof(float));
			}
			else
			{
				double* const nativeOutput =
					block.doubleOutputs()[output.targetChannelIndex];

				for (std::size_t frame = 0; frame < frameCount; ++frame)
				{
					double value =
						output.mode == OutputMode::Add
							? originalTarget[frame]
							: 0.0;

					for (const CompiledOutputTerm& term : output.terms)
					{
						value +=
							runtimePaths[term.sourcePathIndex]
								.scratch[frame] *
							term.gainLinear;
					}

					nativeOutput[frame] =
						value * headroom.appliedTrimLinear;
				}
			}
		}
	}
};

namespace
{

bool prepareSpecsAreEquivalent(
	const PrepareSpec& left,
	const PrepareSpec& right) noexcept
{
	return left.sampleRate == right.sampleRate &&
		left.maximumBlockSize == right.maximumBlockSize &&
		left.channelLayout == right.channelLayout;
}

void validateFinite(double value, const char* message)
{
	if (!std::isfinite(value))
	{
		throw std::invalid_argument(message);
	}
}

}

Processor::Processor()
	: impl_(std::make_unique<Impl>())
{
}

Processor::~Processor() = default;

Processor::Processor(Processor&& other) noexcept = default;

Processor& Processor::operator=(Processor&& other) noexcept = default;

void Processor::prepare(
	const PrepareSpec& prepareSpec,
	const ProcessingGraph& graph)
{
	if (!prepareSpecsAreEquivalent(prepareSpec, graph.prepareSpec()))
	{
		throw std::invalid_argument(
			"PrepareSpec does not match the graph PrepareSpec");
	}

	if (!std::isfinite(prepareSpec.sampleRate) ||
		prepareSpec.sampleRate <= 0.0)
	{
		throw std::invalid_argument("Invalid sample rate");
	}

	if (prepareSpec.maximumBlockSize == 0)
	{
		throw std::invalid_argument("Invalid maximum block size");
	}

	const std::size_t channelCount = prepareSpec.channelLayout.size();

	if (channelCount == 0)
	{
		throw std::invalid_argument("Empty channel layout");
	}

	if (prepareSpec.maximumBlockSize >
		std::numeric_limits<std::size_t>::max() / channelCount)
	{
		throw std::length_error("Channel scratch size overflow");
	}

	const std::size_t planeSampleCount =
		channelCount * prepareSpec.maximumBlockSize;

	auto replacement = std::make_unique<Impl>();
	replacement->prepareSpec = prepareSpec;
	replacement->paths = graph.paths();
	replacement->outputs = graph.outputs();
	replacement->headroom = graph.headroom();

	validateFinite(
		replacement->headroom.appliedTrimLinear,
		"Non-finite headroom trim");

	replacement->runtimePaths.resize(replacement->paths.size());
	replacement->preservedInputs.assign(planeSampleCount, 0.0);
	replacement->floatConversionPlanes.assign(planeSampleCount, 0.0f);
	replacement->targetedChannels.assign(channelCount, 0);

	for (std::size_t pathIndex = 0;
		pathIndex < replacement->paths.size();
		++pathIndex)
	{
		const CompiledPath& path = replacement->paths[pathIndex];
		Impl::RuntimePath& runtimePath =
			replacement->runtimePaths[pathIndex];

		runtimePath.scratch.assign(
			prepareSpec.maximumBlockSize,
			0.0);
		runtimePath.stages.resize(path.stages.size());

		for (const CompiledSourceTerm& source : path.sourceMix)
		{
			if (source.inputChannelIndex >= channelCount)
			{
				throw std::invalid_argument(
					"Compiled source channel index is out of range");
			}

			validateFinite(
				source.gainLinear,
				"Non-finite compiled source gain");
		}

		for (std::size_t stageIndex = 0;
			stageIndex < path.stages.size();
			++stageIndex)
		{
			const CompiledStage& stage = path.stages[stageIndex];
			Impl::RuntimeStage& runtimeStage =
				runtimePath.stages[stageIndex];

			switch (stage.kind)
			{
			case CompiledStageKind::Gain:
				validateFinite(
					stage.gainLinear,
					"Non-finite compiled stage gain");
				break;

			case CompiledStageKind::Delay:
				validateFinite(
					stage.fractionalDelaySamples,
					"Non-finite compiled fractional delay");

				if (stage.fractionalDelaySamples < 0.0 ||
					stage.fractionalDelaySamples >= 1.0)
				{
					throw std::invalid_argument(
						"Compiled fractional delay is outside [0, 1)");
				}

				if (stage.integerDelaySamples >
					std::numeric_limits<std::size_t>::max() - 2)
				{
					throw std::length_error("Delay line size overflow");
				}

				runtimeStage.delayLine.assign(
					stage.integerDelaySamples + 2,
					0.0);
				break;

			case CompiledStageKind::Biquad:
				validateFinite(stage.biquad.b0, "Non-finite biquad b0");
				validateFinite(stage.biquad.b1, "Non-finite biquad b1");
				validateFinite(stage.biquad.b2, "Non-finite biquad b2");
				validateFinite(stage.biquad.a1, "Non-finite biquad a1");
				validateFinite(stage.biquad.a2, "Non-finite biquad a2");
				break;
			}
		}
	}

	for (const CompiledOutput& output : replacement->outputs)
	{
		if (output.targetChannelIndex >= channelCount)
		{
			throw std::invalid_argument(
				"Compiled output channel index is out of range");
		}

		if (replacement->targetedChannels[output.targetChannelIndex] != 0)
		{
			throw std::invalid_argument(
				"Compiled graph has duplicate output targets");
		}

		replacement->targetedChannels[output.targetChannelIndex] = 1;

		for (const CompiledOutputTerm& term : output.terms)
		{
			if (term.sourcePathIndex >= replacement->paths.size())
			{
				throw std::invalid_argument(
					"Compiled output path index is out of range");
			}

			validateFinite(
				term.gainLinear,
				"Non-finite compiled output gain");
		}
	}

	replacement->prepared = true;
	impl_ = std::move(replacement);
}

void Processor::reset() noexcept
{
	if (impl_ != nullptr && impl_->prepared)
	{
		impl_->reset();
	}
}

void Processor::process(const AudioBlock& block) noexcept
{
	if (impl_ != nullptr)
	{
		impl_->process(block);
	}
}

bool Processor::isPrepared() const noexcept
{
	return impl_ != nullptr && impl_->prepared;
}

}
