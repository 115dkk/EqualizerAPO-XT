// MIT License
//
// Portions copyright (c) 2026 weirdsoup (Custom Audio Manager, https://github.com/weirdsoup), MIT License
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

#include "runtime/ipc/SendResampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace eapo::ipc::send
{
	namespace
	{
		constexpr double pi = 3.1415926535897932384626433832795;

		template<typename T>
		size_t checkedElements(size_t frames, size_t channels)
		{
			const size_t limit = std::vector<T>().max_size();
			if (channels == 0 || frames > limit / channels)
				throw std::length_error("SendResampler storage is too large");
			return frames * channels;
		}
	}

	uint32_t SendResampler::filterDelayFrames(double sourceRate, double destinationRate)
	{
		if (!std::isfinite(sourceRate) || !std::isfinite(destinationRate)
			|| sourceRate <= 0.0 || destinationRate <= 0.0)
		{
			throw std::invalid_argument("SendResampler rates must be finite and positive");
		}
		const double delay = std::ceil(48.0 * (std::max)(1.0, sourceRate / destinationRate));
		if (!std::isfinite(delay) || delay > static_cast<double>(UINT32_MAX))
			throw std::invalid_argument("SendResampler rate ratio is not representable");
		return static_cast<uint32_t>(delay);
	}

	void SendResampler::prepare(uint32_t channels, double sourceRate, double destinationRate,
		uint32_t capacityFrames, uint32_t targetFrames)
	{
		if (channels == 0 || capacityFrames == 0 || targetFrames == 0
			|| targetFrames > capacityFrames || !std::isfinite(sourceRate)
			|| !std::isfinite(destinationRate) || sourceRate <= 0.0 || destinationRate <= 0.0)
		{
			throw std::invalid_argument("SendResampler parameters are invalid");
		}

		const double ratio = sourceRate / destinationRate;
		const double maxStep = ratio * (1.0 + maximumCorrectionPpm * 1.0e-6);
		const double inverseSourceRate = 1.0 / sourceRate;
		const double outputPeriod = 1.0 / destinationRate;
		const double slewPerFrame = maximumSlewPpmPerSecond * outputPeriod;
		const uint32_t halfWidth = filterDelayFrames(sourceRate, destinationRate);
		if (!std::isfinite(ratio) || ratio <= 0.0 || !std::isfinite(maxStep)
			|| !std::isfinite(inverseSourceRate) || !std::isfinite(outputPeriod)
			|| !std::isfinite(slewPerFrame) || halfWidth > UINT32_MAX / 2
			|| std::ceil(maxStep) > static_cast<double>(capacityFrames))
		{
			throw std::invalid_argument("SendResampler rate ratio or capacity is unsupported");
		}

		const uint32_t taps = 2 * halfWidth;
		const size_t channelCount = static_cast<size_t>(channels);
		const size_t tapCount = static_cast<size_t>(taps);
		std::vector<double> ring(checkedElements<double>(capacityFrames, channelCount), 0.0);
		std::vector<double> history(checkedElements<double>(tapCount, channelCount), 0.0);
		std::vector<double> coefficients(checkedElements<double>(phaseCount + 1, tapCount));
		std::vector<double> weights(tapCount, 0.0);

		// The cutoff includes the largest permitted correction. This keeps the
		// controller from moving the read phase beyond the kernel's passband.
		const double cutoff = 0.94 * (std::min)(1.0, 1.0 / maxStep);
		for (uint32_t phase = 0; phase <= phaseCount; phase++)
		{
			const double fraction = static_cast<double>(phase) / phaseCount;
			double sum = 0.0;
			for (uint32_t tap = 0; tap < taps; tap++)
			{
				const double distance = static_cast<double>(tap) + fraction - halfWidth;
				const double angle = pi * cutoff * distance;
				const double sinc = std::abs(angle) < 1.0e-12 ? 1.0 : std::sin(angle) / angle;
				const double windowAngle = pi * distance / halfWidth;
				const double window = 0.42 + 0.5 * std::cos(windowAngle)
					+ 0.08 * std::cos(2.0 * windowAngle);
				weights[tap] = cutoff * sinc * window;
				sum += weights[tap];
			}
			if (!std::isfinite(sum) || sum <= 0.0)
				throw std::invalid_argument("SendResampler could not construct its kernel");
			const size_t offset = static_cast<size_t>(phase) * tapCount;
			for (uint32_t tap = 0; tap < taps; tap++)
				coefficients[offset + tap] = weights[tap] / sum;
		}

		std::vector<uint32_t> nonzeroHistory(channelCount, 0);
		ring_.swap(ring);
		history_.swap(history);
		coefficients_.swap(coefficients);
		weights_.swap(weights);
		nonzeroHistory_.swap(nonzeroHistory);
		channels_ = channels;
		capacityFrames_ = capacityFrames;
		targetFrames_ = targetFrames;
		tapCount_ = taps;
		nominalStep_ = ratio;
		inverseSourceRate_ = inverseSourceRate;
		outputPeriod_ = outputPeriod;
		occupancyFilterAlpha_ = -std::expm1(-2.0 * outputPeriod);
		slewPerOutputFrame_ = slewPerFrame;
		reset();
	}

	void SendResampler::clearConsumerState() noexcept
	{
		std::fill(history_.begin(), history_.end(), 0.0);
		std::fill(nonzeroHistory_.begin(), nonzeroHistory_.end(), 0);
		historyNext_ = 0;
		sourcePhase_ = 1.0;
		filteredOccupancy_ = static_cast<double>(targetFrames_);
		integralPpm_ = 0.0;
		correctionPpm_ = 0.0;
		lastWeightFraction_ = -1.0;
		primed_ = false;
	}

	void SendResampler::reset() noexcept
	{
		producerWrite_ = 0;
		consumerRead_ = 0;
		producerFrame_ = 0;
		consumerFrame_ = 0;
		underruns_ = 0;
		overruns_ = 0;
		overflowPending_ = false;
		clearConsumerState();
	}

	void SendResampler::discardQueued() noexcept
	{
		consumerRead_ = producerWrite_;
		consumerFrame_ = producerFrame_;
		overflowPending_ = false;
		clearConsumerState();
	}

	uint32_t SendResampler::push(const float* const* channels, uint32_t frameCount) noexcept
	{
		if (channels_ == 0 || frameCount == 0)
			return 0;
		const uint64_t used = producerWrite_ - consumerRead_;
		const uint64_t free = static_cast<uint64_t>(capacityFrames_) - used;
		const uint32_t accepted = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(frameCount), free));
		const size_t channelCount = static_cast<size_t>(channels_);
		for (uint32_t frame = 0; frame < accepted; frame++)
		{
			double* destination = ring_.data() + static_cast<size_t>(producerFrame_) * channelCount;
			for (uint32_t channel = 0; channel < channels_; channel++)
			{
				const float sample = channels != nullptr && channels[channel] != nullptr
					? channels[channel][frame] : 0.0f;
				destination[channel] = std::isfinite(sample) ? static_cast<double>(sample) : 0.0;
			}
			if (++producerFrame_ == capacityFrames_)
				producerFrame_ = 0;
		}
		producerWrite_ += accepted;
		if (accepted < frameCount)
		{
			overruns_++;
			overflowPending_ = true;
		}
		return accepted;
	}

	void SendResampler::updateCorrection(uint32_t bufferedFrames) noexcept
	{
		filteredOccupancy_ += occupancyFilterAlpha_
			* (static_cast<double>(bufferedFrames) - filteredOccupancy_);
		double desiredPpm = 0.0;
		if (driftCorrectionEnabled_)
		{
			const double errorSeconds = (filteredOccupancy_ - targetFrames_) * inverseSourceRate_;
			const double proportionalPpm = 0.3e6 * errorSeconds;
			const double candidate = (std::clamp)(integralPpm_ + 0.02e6 * errorSeconds * outputPeriod_,
				-maximumCorrectionPpm, maximumCorrectionPpm);
			const double requested = proportionalPpm + candidate;
			if ((requested >= -maximumCorrectionPpm && requested <= maximumCorrectionPpm)
				|| (requested > maximumCorrectionPpm && errorSeconds < 0.0)
				|| (requested < -maximumCorrectionPpm && errorSeconds > 0.0))
			{
				integralPpm_ = candidate;
			}
			desiredPpm = (std::clamp)(proportionalPpm + integralPpm_,
				-maximumCorrectionPpm, maximumCorrectionPpm);
		}
		else
		{
			integralPpm_ = 0.0;
		}
		correctionPpm_ += (std::clamp)(desiredPpm - correctionPpm_,
			-slewPerOutputFrame_, slewPerOutputFrame_);
		correctionPpm_ = (std::clamp)(correctionPpm_, -maximumCorrectionPpm, maximumCorrectionPpm);
	}

	SendResampler::RenderStatus SendResampler::render(double* const* channels,
		uint32_t frameCount, bool add) noexcept
	{
		if (channels_ == 0 || channels == nullptr || frameCount == 0)
			return RenderStatus::Priming;

		bool hasOutput = false;
		for (uint32_t channel = 0; channel < channels_; channel++)
		{
			if (channels[channel] == nullptr)
				continue;
			hasOutput = true;
			if (!add)
				std::fill_n(channels[channel], frameCount, 0.0);
		}

		if (overflowPending_)
		{
			discardQueued();
			underruns_++;
			return RenderStatus::Underrun;
		}
		if (!primed_)
		{
			if (producerWrite_ - consumerRead_ < targetFrames_)
				return RenderStatus::Priming;
			primed_ = true;
		}

		const size_t channelCount = static_cast<size_t>(channels_);
		const size_t taps = static_cast<size_t>(tapCount_);
		for (uint32_t frame = 0; frame < frameCount; frame++)
		{
			const uint32_t advance = static_cast<uint32_t>(sourcePhase_);
			const double fraction = sourcePhase_ - advance;
			const uint64_t available = producerWrite_ - consumerRead_;
			if (available < advance)
			{
				underruns_++;
				discardQueued();
				return RenderStatus::Underrun;
			}

			for (uint32_t inputFrame = 0; inputFrame < advance; inputFrame++)
			{
				const double* input = ring_.data() + static_cast<size_t>(consumerFrame_) * channelCount;
				double* history = history_.data() + static_cast<size_t>(historyNext_) * channelCount;
				for (uint32_t channel = 0; channel < channels_; channel++)
				{
					nonzeroHistory_[channel] += static_cast<uint32_t>(input[channel] != 0.0);
					nonzeroHistory_[channel] -= static_cast<uint32_t>(history[channel] != 0.0);
					history[channel] = input[channel];
				}
				if (++consumerFrame_ == capacityFrames_)
					consumerFrame_ = 0;
				if (++historyNext_ == tapCount_)
					historyNext_ = 0;
			}
			consumerRead_ += advance;

			if (hasOutput)
			{
				if (fraction != lastWeightFraction_)
				{
					lastWeightFraction_ = fraction;
					const double tablePosition = fraction * phaseCount;
					const uint32_t phase = (std::min)(static_cast<uint32_t>(tablePosition), phaseCount - 1);
					const double blend = tablePosition - phase;
					const size_t offset = static_cast<size_t>(phase) * taps;
					for (uint32_t tap = 0; tap < tapCount_; tap++)
					{
						const double a = coefficients_[offset + tap];
						const double b = coefficients_[offset + taps + tap];
						weights_[tap] = a + blend * (b - a);
					}
				}
				for (uint32_t channel = 0; channel < channels_; channel++)
				{
					if (channels[channel] == nullptr || nonzeroHistory_[channel] == 0)
						continue;
					double value = 0.0;
					uint32_t tap = 0;
					for (uint32_t historyFrame = historyNext_; historyFrame > 0;)
					{
						--historyFrame;
						value += history_[static_cast<size_t>(historyFrame) * channelCount + channel]
							* weights_[tap++];
					}
					for (uint32_t historyFrame = tapCount_; tap < tapCount_;)
					{
						--historyFrame;
						value += history_[static_cast<size_t>(historyFrame) * channelCount + channel]
							* weights_[tap++];
					}
					if (!std::isfinite(value))
						value = 0.0;
					if (add)
						channels[channel][frame] += value;
					else
						channels[channel][frame] = value;
				}
			}

			const uint64_t remaining = producerWrite_ - consumerRead_;
			updateCorrection(static_cast<uint32_t>((std::min)(remaining,
				static_cast<uint64_t>(UINT32_MAX))));
			sourcePhase_ = fraction + nominalStep_ * (1.0 + correctionPpm_ * 1.0e-6);
		}
		return RenderStatus::Delivered;
	}

	SendResampler::Stats SendResampler::stats() const noexcept
	{
		Stats result;
		result.correctionPpm = correctionPpm_;
		result.bufferedFrames = static_cast<uint32_t>((std::min)(producerWrite_ - consumerRead_,
			static_cast<uint64_t>(UINT32_MAX)));
		result.consumedFrames = consumerRead_;
		result.underruns = underruns_;
		result.overruns = overruns_;
		result.primed = primed_;
		return result;
	}
}
