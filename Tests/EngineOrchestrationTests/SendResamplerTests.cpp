/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#include "runtime/ipc/SendResampler.h"
#include "runtime/ipc/SendRing.h"

#include "EngineOrchestrationTestSupport.h"

#ifndef __SANITIZE_ADDRESS__
namespace send_resampler_allocation_test
{
	std::atomic<size_t> count{0};

	void* allocate(size_t size)
	{
		count.fetch_add(1, std::memory_order_relaxed);
		if (void* memory = std::malloc(size == 0 ? 1 : size))
			return memory;
		throw std::bad_alloc();
	}

	void* allocateAligned(size_t size, size_t alignment)
	{
		const size_t overhead = alignment - 1 + sizeof(void*);
		if (size > (std::numeric_limits<size_t>::max)() - overhead)
			throw std::bad_alloc();
		void* raw = allocate(size + overhead);
		const uintptr_t base = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
		const uintptr_t aligned = (base + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
		void* result = reinterpret_cast<void*>(aligned);
		reinterpret_cast<void**>(result)[-1] = raw;
		return result;
	}

	void freeAligned(void* pointer) noexcept
	{
		if (pointer != nullptr)
			std::free(reinterpret_cast<void**>(pointer)[-1]);
	}
}

void* operator new(size_t size) {return send_resampler_allocation_test::allocate(size);}
void* operator new[](size_t size) {return send_resampler_allocation_test::allocate(size);}
void operator delete(void* pointer) noexcept {std::free(pointer);}
void operator delete[](void* pointer) noexcept {std::free(pointer);}
void operator delete(void* pointer, size_t) noexcept {std::free(pointer);}
void operator delete[](void* pointer, size_t) noexcept {std::free(pointer);}
void* operator new(size_t size, std::align_val_t alignment)
{
	return send_resampler_allocation_test::allocateAligned(size, static_cast<size_t>(alignment));
}
void* operator new[](size_t size, std::align_val_t alignment)
{
	return send_resampler_allocation_test::allocateAligned(size, static_cast<size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept
{
	send_resampler_allocation_test::freeAligned(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept
{
	send_resampler_allocation_test::freeAligned(pointer);
}
void operator delete(void* pointer, size_t, std::align_val_t) noexcept
{
	send_resampler_allocation_test::freeAligned(pointer);
}
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept
{
	send_resampler_allocation_test::freeAligned(pointer);
}
#endif

namespace
{
	using eapo::ipc::send::SendResampler;
	constexpr double pi = 3.1415926535897932384626433832795;

	void fillSine(std::vector<float>& samples, uint64_t firstFrame,
		double sampleRate, double frequency, double amplitude)
	{
		for (size_t frame = 0; frame < samples.size(); frame++)
		{
			samples[frame] = static_cast<float>(amplitude * std::sin(
				2.0 * pi * frequency * static_cast<double>(firstFrame + frame) / sampleRate));
		}
	}

	void checkDcGain(test::Harness& harness)
	{
		SendResampler resampler;
		resampler.prepare(1, 48'000.0, 48'000.0, 4096, 960);
		std::vector<float> input(2048, 0.25f);
		const float* inputPlanes[] = {input.data()};
		harness.requireEqual(resampler.push(inputPlanes, static_cast<uint32_t>(input.size())),
			static_cast<uint32_t>(input.size()), "DC prefill fits the resampler queue");
		std::vector<double> output(1024, 0.0);
		double* outputPlanes[] = {output.data()};
		harness.require(resampler.render(outputPlanes, static_cast<uint32_t>(output.size()), false)
			== SendResampler::RenderStatus::Delivered, "DC block renders without starvation");
		for (size_t frame = 128; frame < output.size(); frame++)
			harness.expectNear(output[frame], 0.25, 2.0e-9, "resampler preserves DC gain");
	}

	void checkSineConversion(test::Harness& harness)
	{
		constexpr double sourceRate = 44'100.0;
		constexpr double destinationRate = 48'000.0;
		constexpr double frequency = 1000.0;
		constexpr uint32_t target = 960;
		constexpr uint32_t block = 128;
		constexpr uint32_t outputFrames = 12'288;
		SendResampler resampler;
		resampler.prepare(1, sourceRate, destinationRate, 4096, target);
		resampler.setDriftCorrection(false);

		std::vector<float> source(target);
		fillSine(source, 0, sourceRate, frequency, 0.6);
		const float* sourcePlanes[] = {source.data()};
		harness.requireEqual(resampler.push(sourcePlanes, target), target,
			"sine prefill reaches the controller target");
		uint64_t nextSource = target;
		double arrivals = 0.0;
		std::vector<double> rendered(outputFrames, 0.0);
		std::vector<double> blockOutput(block, 0.0);
		double* outputPlanes[] = {blockOutput.data()};
		for (uint32_t offset = 0; offset < outputFrames; offset += block)
		{
			arrivals += block * sourceRate / destinationRate;
			const uint32_t captured = static_cast<uint32_t>(arrivals);
			arrivals -= captured;
			source.resize(captured);
			fillSine(source, nextSource, sourceRate, frequency, 0.6);
			sourcePlanes[0] = source.data();
			harness.requireEqual(resampler.push(sourcePlanes, captured), captured,
				"nominal sine input does not overflow");
			nextSource += captured;
			harness.require(resampler.render(outputPlanes, block, false)
				== SendResampler::RenderStatus::Delivered,
				"44.1 kHz to 48 kHz sine conversion stays primed");
			std::copy(blockOutput.begin(), blockOutput.end(), rendered.begin() + offset);
		}

		constexpr uint32_t settled = 512;
		double sineSquare = 0.0;
		double cosineSquare = 0.0;
		double sineCosine = 0.0;
		double sineProjection = 0.0;
		double cosineProjection = 0.0;
		for (uint32_t frame = settled; frame < outputFrames; frame++)
		{
			const double angle = 2.0 * pi * frequency * frame / destinationRate;
			const double sine = std::sin(angle);
			const double cosine = std::cos(angle);
			sineSquare += sine * sine;
			cosineSquare += cosine * cosine;
			sineCosine += sine * cosine;
			sineProjection += rendered[frame] * sine;
			cosineProjection += rendered[frame] * cosine;
		}
		const double determinant = sineSquare * cosineSquare - sineCosine * sineCosine;
		const double sineGain = (sineProjection * cosineSquare
			- cosineProjection * sineCosine) / determinant;
		const double cosineGain = (cosineProjection * sineSquare
			- sineProjection * sineCosine) / determinant;
		const double measuredAmplitude = std::sqrt(sineGain * sineGain + cosineGain * cosineGain);
		double residualEnergy = 0.0;
		double signalEnergy = 0.0;
		for (uint32_t frame = settled; frame < outputFrames; frame++)
		{
			const double angle = 2.0 * pi * frequency * frame / destinationRate;
			const double fitted = sineGain * std::sin(angle) + cosineGain * std::cos(angle);
			const double residual = rendered[frame] - fitted;
			residualEnergy += residual * residual;
			signalEnergy += fitted * fitted;
		}
		harness.expectNear(measuredAmplitude, 0.6, 5.0e-4,
			"44.1 kHz to 48 kHz preserves the 1 kHz sine amplitude");
		harness.expectNear(std::sqrt(residualEnergy / signalEnergy), 0.0, 1.0e-3,
			"44.1 kHz to 48 kHz keeps the sine at 1 kHz with low residual distortion");
	}

	void checkDrift(test::Harness& harness, double clockPpm)
	{
		constexpr double sourceRate = 48'000.0;
		constexpr double destinationRate = 48'000.0;
		constexpr uint32_t capacity = 4096;
		constexpr uint32_t target = 1536;
		constexpr double durationSeconds = 600.0;
		const uint32_t blocks[] = {64, 127, 256, 73};
		SendResampler resampler;
		resampler.prepare(1, sourceRate, destinationRate, capacity, target);
		std::vector<float> input(target, 0.25f);
		const float* inputPlanes[] = {input.data()};
		harness.requireEqual(resampler.push(inputPlanes, target), target,
			"drift simulation prefill reaches the target");
		double* discarded[] = {nullptr};

		const uint64_t totalFrames = static_cast<uint64_t>(durationSeconds * destinationRate);
		uint64_t rendered = 0;
		uint64_t callback = 0;
		double arrivals = 0.0;
		double previousCorrection = 0.0;
		double finalCorrectionIntegral = 0.0;
		double finalSeconds = 0.0;
		uint32_t settledLow = capacity;
		uint32_t settledHigh = 0;
		bool bounded = true;
		bool slewBounded = true;
		bool uninterrupted = true;
		while (rendered < totalFrames)
		{
			const uint32_t frames = static_cast<uint32_t>((std::min)(
				static_cast<uint64_t>(blocks[callback % std::size(blocks)]), totalFrames - rendered));
			arrivals += frames * (1.0 + clockPpm * 1.0e-6);
			const uint32_t captured = static_cast<uint32_t>(arrivals);
			arrivals -= captured;
			input.resize(captured, 0.25f);
			inputPlanes[0] = input.data();
			if (resampler.push(inputPlanes, captured) != captured)
				uninterrupted = false;
			if (resampler.render(discarded, frames, false) != SendResampler::RenderStatus::Delivered)
				uninterrupted = false;
			const SendResampler::Stats state = resampler.stats();
			bounded = bounded && state.bufferedFrames > 0 && state.bufferedFrames < capacity;
			const double allowedSlew = SendResampler::maximumSlewPpmPerSecond
				* frames / destinationRate + 1.0e-8;
			slewBounded = slewBounded
				&& std::abs(state.correctionPpm - previousCorrection) <= allowedSlew;
			previousCorrection = state.correctionPpm;
			const double elapsed = static_cast<double>(rendered) / destinationRate;
			if (elapsed > 120.0)
			{
				settledLow = (std::min)(settledLow, state.bufferedFrames);
				settledHigh = (std::max)(settledHigh, state.bufferedFrames);
			}
			if (elapsed > durationSeconds - 60.0)
			{
				finalCorrectionIntegral += state.correctionPpm * frames / destinationRate;
				finalSeconds += frames / destinationRate;
			}
			rendered += frames;
			callback++;
		}
		const SendResampler::Stats final = resampler.stats();
		harness.expect(uninterrupted && final.underruns == 0 && final.overruns == 0,
			"ten-minute drift simulation has no underrun or overflow after startup");
		harness.expect(bounded, "drift correction keeps the queue inside its capacity");
		harness.expect(slewBounded, "drift correction never exceeds 200 ppm per second slew");
		harness.expectNear(finalCorrectionIntegral / finalSeconds, clockPpm, 12.0,
			"PI controller learns the simulated clock error");
		harness.expect(settledLow > target - 512 && settledHigh < target + 512,
			"settled queue occupancy remains near its target");
	}

	void checkNonfiniteInput(test::Harness& harness)
	{
		SendResampler resampler;
		resampler.prepare(1, 48'000.0, 48'000.0, 512, 64);
		std::vector<float> input(256);
		for (size_t frame = 0; frame < input.size(); frame++)
		{
			input[frame] = frame % 3 == 0 ? (std::numeric_limits<float>::quiet_NaN)()
				: (frame % 3 == 1 ? (std::numeric_limits<float>::infinity)()
					: -(std::numeric_limits<float>::infinity)());
		}
		const float* inputPlanes[] = {input.data()};
		harness.requireEqual(resampler.push(inputPlanes, static_cast<uint32_t>(input.size())),
			static_cast<uint32_t>(input.size()), "non-finite input fits the queue");
		std::vector<double> output(128, 9.0);
		double* outputPlanes[] = {output.data()};
		harness.require(resampler.render(outputPlanes, static_cast<uint32_t>(output.size()), false)
			== SendResampler::RenderStatus::Delivered, "non-finite input renders");
		harness.expect(std::all_of(output.begin(), output.end(), [](double sample) {
			return sample == 0.0;
		}), "NaN and infinity are scrubbed to exact silence");
	}
}

TEST_CASE(EngineOrchestrationTests, testSendResamplerDcGainAndSineConversion)
{
	checkDcGain(harness);
	checkSineConversion(harness);
}

TEST_CASE(EngineOrchestrationTests, testSendResamplerTracksPositiveAndNegativeClockDrift)
{
	checkDrift(harness, 500.0);
	checkDrift(harness, -500.0);
}

TEST_CASE(EngineOrchestrationTests, testSendResamplerScrubsNonfiniteInput)
{
	checkNonfiniteInput(harness);
}

TEST_CASE(EngineOrchestrationTests, testSendResamplerReadPathAllocatesNothing)
{
#ifdef __SANITIZE_ADDRESS__
	harness.skip("the allocation replacement is disabled under AddressSanitizer because the runtime owns operator new");
	return;
#else
	using namespace eapo::ipc::send;
	std::vector<unsigned char> storage(regionBytes + 63, 0);
	const uintptr_t address = reinterpret_cast<uintptr_t>(storage.data());
	void* region = reinterpret_cast<void*>((address + 63) & ~uintptr_t(63));
	SendWriterCore writer(region);
	SendParams params;
	params.senderId = 91;
	params.sampleRate = 44'100.0;
	params.delayFrames = 960;
	params.senderMaxFrameCount = 128;
	params.mixMode = MixMode::Replace;
	params.targetNames = {L"L", L"R"};
	constexpr int64_t qpcFrequency = 10'000'000;
	writer.publish(params, qpcFrequency);

	std::array<std::vector<float>, 2> input{
		std::vector<float>(1024, 0.1f), std::vector<float>(1024, -0.2f)};
	const float* inputPlanes[] = {input[0].data(), input[1].data()};
	writer.write(inputPlanes, 2, 1024, 100);
	const AttachPlan plan = prepareAttach(region, 48'000.0, 128, {L"L", L"R"}, 100, true);
	harness.require(plan.ok, "allocation test attaches the different-rate resampling reader");
	SendReaderCore reader(region, plan, 48'000.0, 128);
	std::array<std::vector<double>, 2> output{
		std::vector<double>(128), std::vector<double>(128)};
	double* outputPlanes[] = {output[0].data(), output[1].data()};

	const size_t before = send_resampler_allocation_test::count.load(std::memory_order_relaxed);
	for (int iteration = 0; iteration < 4; iteration++)
	{
		writer.write(inputPlanes, 2, 117, 200 + iteration);
		(void)reader.read(outputPlanes, 2, 128, 200 + iteration);
		(void)reader.underruns();
	}
	const size_t after = send_resampler_allocation_test::count.load(std::memory_order_relaxed);
	harness.expectEqual(after, before, "resampling SendReaderCore::read allocates nothing");
#endif
}
