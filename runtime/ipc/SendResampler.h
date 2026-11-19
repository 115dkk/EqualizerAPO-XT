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

#pragma once

#include <cstdint>
#include <vector>

namespace eapo::ipc::send
{
	// Fractional source-rate conversion for a Send receiver. prepare() performs
	// every allocation and builds the polyphase sinc table. push() and render()
	// are realtime-safe and use no locks, allocation, clocks, or trigonometry.
	class SendResampler
	{
	public:
		static constexpr double maximumCorrectionPpm = 1000.0;
		static constexpr double maximumSlewPpmPerSecond = 200.0;
		static constexpr uint32_t phaseCount = 1024;

		enum class RenderStatus
		{
			Delivered,
			Priming,
			Underrun
		};

		struct Stats
		{
			double correctionPpm = 0.0;
			uint32_t bufferedFrames = 0;
			uint64_t consumedFrames = 0;
			uint64_t underruns = 0;
			uint64_t overruns = 0;
			bool primed = false;
		};

		// The causal kernel has twice this many taps. At equal rates and while
		// upsampling it is 48 source frames; downsampling scales it by the nominal
		// source/destination ratio so the 6 percent transition band remains useful.
		static uint32_t filterDelayFrames(double sourceRate, double destinationRate);

		void prepare(uint32_t channels, double sourceRate, double destinationRate,
			uint32_t capacityFrames, uint32_t targetFrames);
		void reset() noexcept;
		void setDriftCorrection(bool enabled) noexcept {driftCorrectionEnabled_ = enabled;}

		uint32_t push(const float* const* channels, uint32_t frameCount) noexcept;
		// Null planes discard their channel while time advances. When add is false,
		// every non-null destination is cleared before rendering; when true, rendered
		// samples are added and silence leaves the destination unchanged.
		RenderStatus render(double* const* channels, uint32_t frameCount, bool add) noexcept;

		Stats stats() const noexcept;

	private:
		void clearConsumerState() noexcept;
		void discardQueued() noexcept;
		void updateCorrection(uint32_t bufferedFrames) noexcept;

		uint32_t channels_ = 0;
		uint32_t capacityFrames_ = 0;
		uint32_t targetFrames_ = 0;
		uint32_t tapCount_ = 0;
		double nominalStep_ = 1.0;
		double inverseSourceRate_ = 0.0;
		double outputPeriod_ = 0.0;
		double occupancyFilterAlpha_ = 0.0;
		double slewPerOutputFrame_ = 0.0;

		std::vector<double> ring_;
		std::vector<double> history_;
		std::vector<double> coefficients_;
		std::vector<double> weights_;
		std::vector<uint32_t> nonzeroHistory_;

		uint64_t producerWrite_ = 0;
		uint64_t consumerRead_ = 0;
		uint32_t producerFrame_ = 0;
		uint32_t consumerFrame_ = 0;
		uint32_t historyNext_ = 0;
		double sourcePhase_ = 1.0;
		double filteredOccupancy_ = 0.0;
		double integralPpm_ = 0.0;
		double correctionPpm_ = 0.0;
		double lastWeightFraction_ = -1.0;
		uint64_t underruns_ = 0;
		uint64_t overruns_ = 0;
		bool primed_ = false;
		bool overflowPending_ = false;
		bool driftCorrectionEnabled_ = true;
	};
}
