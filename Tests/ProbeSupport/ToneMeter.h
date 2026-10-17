/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Sine generation and level measurements shared by the audio probes.
*/

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace probesupport
{
	inline double sineSample(double frequency, double amplitude, uint64_t frame, double sampleRate)
	{
		return amplitude * std::sin(2.0 * std::numbers::pi_v<double> * frequency
			* static_cast<double>(frame) / sampleRate);
	}

	template<typename Sample>
	void fillInterleavedSine(Sample* samples, size_t frames, size_t channels, uint64_t& frame,
		double sampleRate, double frequency, double amplitude)
	{
		for (size_t f = 0; f < frames; f++, frame++)
		{
			const Sample value = static_cast<Sample>(sineSample(frequency, amplitude, frame, sampleRate));
			for (size_t channel = 0; channel < channels; channel++)
				samples[f * channels + channel] = value;
		}
	}

	template<typename Sample>
	void fillPlanarSine(Sample* const* samples, size_t channels, size_t frames, uint64_t firstFrame,
		double sampleRate, double frequency, double amplitude)
	{
		for (size_t f = 0; f < frames; f++)
		{
			const Sample value = static_cast<Sample>(sineSample(frequency, amplitude, firstFrame + f, sampleRate));
			for (size_t channel = 0; channel < channels; channel++)
				samples[channel][f] = value;
		}
	}

	inline double amplitudeToDb(double amplitude)
	{
		if (amplitude <= 0.0)
			return -200.0;
		return 20.0 * std::log10(amplitude);
	}

	class ToneLevelMeter
	{
	public:
		ToneLevelMeter(size_t channels, double sampleRate, double frequency)
			: sumSquares_(channels, 0.0), coefficient_(2.0 * std::cos(2.0 * std::numbers::pi_v<double>
				* frequency / sampleRate))
		{
		}

		void addFrame(const float* samples, bool silent)
		{
			for (size_t channel = 0; channel < sumSquares_.size(); channel++)
			{
				const double value = silent ? 0.0 : static_cast<double>(samples[channel]);
				sumSquares_[channel] += value * value;
				if (channel == 0)
				{
					const double next = value + coefficient_ * state1_ - state2_;
					state2_ = state1_;
					state1_ = next;
				}
			}
			frames_++;
		}

		uint64_t frames() const noexcept
		{
			return frames_;
		}

		double toneAmplitude() const
		{
			if (frames_ == 0)
				return 0.0;
			const double power = state1_ * state1_ + state2_ * state2_ - coefficient_ * state1_ * state2_;
			return 2.0 * std::sqrt(power < 0.0 ? 0.0 : power) / static_cast<double>(frames_);
		}

		std::vector<double> rmsDb() const
		{
			std::vector<double> result(sumSquares_.size(), -200.0);
			if (frames_ == 0)
				return result;
			for (size_t channel = 0; channel < sumSquares_.size(); channel++)
				result[channel] = amplitudeToDb(std::sqrt(sumSquares_[channel] / static_cast<double>(frames_)));
			return result;
		}

	private:
		std::vector<double> sumSquares_;
		double coefficient_ = 0.0;
		double state1_ = 0.0;
		double state2_ = 0.0;
		uint64_t frames_ = 0;
	};
}
