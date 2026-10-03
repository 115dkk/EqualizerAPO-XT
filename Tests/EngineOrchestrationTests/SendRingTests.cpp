/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "runtime/ipc/SendRing.h"
#include "runtime/ipc/SendRingWin32.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
	using namespace eapo::ipc::send;

	constexpr int64_t qpcFrequency = 10'000'000;
	constexpr uint32_t sampleRate = 48'000;
	constexpr uint32_t blockFrames = 480;
	constexpr int64_t blockTicks = 100'000;

	class AlignedRegion
	{
	public:
		AlignedRegion()
			: storage_(regionBytes + 63, 0)
		{
			const uintptr_t address = reinterpret_cast<uintptr_t>(storage_.data());
			base_ = reinterpret_cast<void*>((address + 63) & ~uintptr_t(63));
		}

		void* get() noexcept
		{
			return base_;
		}

		Header* header() noexcept
		{
			return static_cast<Header*>(base_);
		}

	private:
		std::vector<unsigned char> storage_;
		void* base_ = nullptr;
	};

	uint32_t load32(const volatile LONG* value)
	{
		return static_cast<uint32_t>(ReadAcquire(value));
	}

	uint64_t load64(const volatile LONG64* value)
	{
		return static_cast<uint64_t>(ReadAcquire64(value));
	}

	int64_t loadSigned64(const volatile LONG64* value)
	{
		return static_cast<int64_t>(ReadAcquire64(value));
	}

	void store32(volatile LONG* destination, uint32_t value)
	{
		WriteRelease(destination, static_cast<LONG>(value));
	}

	void store64(volatile LONG64* destination, uint64_t value)
	{
		WriteRelease64(destination, static_cast<LONG64>(value));
	}

	SendParams params(uint64_t senderId, uint32_t delay, MixMode mode,
		std::vector<std::wstring> names)
	{
		SendParams result;
		result.senderId = senderId;
		result.sampleRate = sampleRate;
		result.delayFrames = delay;
		result.senderMaxFrameCount = blockFrames;
		result.mixMode = mode;
		result.targetNames = std::move(names);
		return result;
	}

	float rampSample(uint32_t channel, int64_t position)
	{
		return static_cast<float>(int64_t(channel) * 1'000'000 + position);
	}

	void writeRamp(SendWriterCore& writer, int64_t startPosition, uint32_t frames,
		uint32_t channels, int64_t nowQpc)
	{
		std::vector<std::vector<float>> samples(channels, std::vector<float>(frames));
		std::vector<const float*> planes(channels);
		for (uint32_t channel = 0; channel < channels; channel++)
		{
			for (uint32_t frame = 0; frame < frames; frame++)
				samples[channel][frame] = rampSample(channel, startPosition + frame);
			planes[channel] = samples[channel].data();
		}
		writer.write(planes.data(), channels, frames, nowQpc);
	}

	void writeMono(SendWriterCore& writer, const std::vector<float>& samples, int64_t nowQpc)
	{
		const float* plane = samples.data();
		writer.write(&plane, 1, static_cast<uint32_t>(samples.size()), nowQpc);
	}

	AttachPlan attach(AlignedRegion& region, const std::vector<std::wstring>& deviceChannels,
		int64_t nowQpc, uint32_t maxFrames = blockFrames)
	{
		return prepareAttach(region.get(), sampleRate, maxFrames, deviceChannels, nowQpc);
	}

	void checkLayoutAndNames(test::Harness& harness)
	{
		harness.expectEqual(sizeof(Header), headerBytes, "Send header has the pinned byte size");
		harness.expectEqual(offsetof(Header, writePos) % 64, size_t(0),
			"writer cursor starts on a cache line");
		harness.expectEqual(offsetof(Header, readPos) % 64, size_t(0),
			"reader diagnostics start on a cache line");
		harness.expect(offsetof(Header, writePos) != offsetof(Header, readPos),
			"writer and reader fields use different cache lines");
		harness.expectEqual(regionBytes, size_t(2'099'200), "Send mapping has the fixed byte size");
		harness.expect(mappingName(L"Local\\EAPO.Send.", L"{abc}")
			== L"Local\\EAPO.Send.{abc}", "mapping name appends the canonical GUID");
		harness.expect(eventName(L"Local\\EAPO.Send.", L"{abc}")
			== L"Local\\EAPO.Send.{abc}.ready", "event name adds the ready suffix");
	}

	void checkDelayedReadWithPhase(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(1, 960, MixMode::Mix, {L"L", L"R"}), qpcFrequency);
		for (int block = 0; block < 4; block++)
			writeRamp(writer, int64_t(block) * blockFrames, blockFrames, 2, int64_t(block) * blockTicks);

		const int64_t attachQpc = 3 * blockTicks + 30'000;
		const AttachPlan plan = attach(region, {L"L", L"R"}, attachQpc);
		harness.require(plan.ok, "phase-offset reader could not attach");
		const int64_t expectedStart = 4 * blockFrames + 144 - 960;
		harness.expectEqual(plan.readPos, expectedStart,
			"attach accounts for the three-millisecond writer phase");
		SendReaderCore reader(region.get(), plan, sampleRate, blockFrames);

		std::array<std::vector<double>, 2> output{
			std::vector<double>(blockFrames, 0.0), std::vector<double>(blockFrames, 0.0)};
		double* channels[] = {output[0].data(), output[1].data()};
		bool exact = true;
		int64_t expectedPosition = expectedStart;
		for (int block = 0; block < 8; block++)
		{
			if (block != 0)
			{
				const int64_t position = int64_t(3 + block) * blockFrames;
				writeRamp(writer, position, blockFrames, 2, int64_t(3 + block) * blockTicks);
			}
			std::fill(output[0].begin(), output[0].end(), 0.0);
			std::fill(output[1].begin(), output[1].end(), 0.0);
			const auto status = reader.read(channels, 2, blockFrames, attachQpc + int64_t(block) * blockTicks);
			exact = exact && status == SendReaderCore::Status::Delivered;
			for (uint32_t channel = 0; channel < 2; channel++)
			{
				for (uint32_t frame = 0; frame < blockFrames; frame++)
				{
					exact = exact && output[channel][frame]
						== static_cast<double>(rampSample(channel, expectedPosition + frame));
				}
			}
			expectedPosition += blockFrames;
		}
		harness.expect(exact, "two-channel ramp is sample-exact at the requested 960-frame delay");
	}

	void checkWrap(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(2, 960, MixMode::Mix, {L"L", L"R"}), qpcFrequency);
		int64_t writePosition = 0;
		int64_t nowQpc = 0;
		for (int block = 0; block < 4; block++)
		{
			writeRamp(writer, writePosition, blockFrames, 2, nowQpc);
			writePosition += blockFrames;
			nowQpc += blockTicks;
		}

		const AttachPlan plan = attach(region, {L"L", L"R"}, nowQpc - blockTicks);
		harness.require(plan.ok, "wrap reader could not attach");
		SendReaderCore reader(region.get(), plan, sampleRate, blockFrames);
		std::array<std::vector<double>, 2> output{
			std::vector<double>(blockFrames), std::vector<double>(blockFrames)};
		double* channels[] = {output[0].data(), output[1].data()};
		int64_t expectedPosition = plan.readPos;
		bool exact = true;
		for (int block = 0; block < 240; block++)
		{
			writeRamp(writer, writePosition, blockFrames, 2, nowQpc);
			writePosition += blockFrames;
			std::fill(output[0].begin(), output[0].end(), 0.0);
			std::fill(output[1].begin(), output[1].end(), 0.0);
			const auto status = reader.read(channels, 2, blockFrames, nowQpc);
			exact = exact && status == SendReaderCore::Status::Delivered;
			for (uint32_t channel = 0; channel < 2; channel++)
			{
				for (uint32_t frame = 0; frame < blockFrames; frame++)
				{
					exact = exact && output[channel][frame]
						== static_cast<double>(rampSample(channel, expectedPosition + frame));
				}
			}
			expectedPosition += blockFrames;
			nowQpc += blockTicks;
		}
		harness.expect(exact, "ramp remains sample-exact across more than three ring wraps");
	}

	void checkMixAndReplace(test::Harness& harness)
	{
		for (MixMode mode : {MixMode::Mix, MixMode::Replace})
		{
			AlignedRegion region;
			SendWriterCore writer(region.get());
			writer.publish(params(mode == MixMode::Mix ? 3 : 4, 4, mode, {L"L"}), qpcFrequency);
			writeMono(writer, {1.0f, 2.0f, 3.0f, 4.0f}, 100);
			const AttachPlan plan = attach(region, {L"L", L"R"}, 100, 1);
			harness.require(plan.ok, "mix-mode reader could not attach");
			SendReaderCore reader(region.get(), plan, sampleRate, 1);
			std::array<double, 4> left = {10.0, 10.0, 10.0, 10.0};
			std::array<double, 4> right = {20.0, 20.0, 20.0, 20.0};
			double* channels[] = {left.data(), right.data()};
			harness.expect(reader.read(channels, 2, 4, 100) == SendReaderCore::Status::Delivered,
				"mix-mode block is delivered");
			for (size_t frame = 0; frame < left.size(); frame++)
			{
				const double expected = mode == MixMode::Mix ? 11.0 + frame : 1.0 + frame;
				harness.expectEqual(left[frame], expected,
					mode == MixMode::Mix ? "Mix adds to the target" : "Replace overwrites the target");
				harness.expectEqual(right[frame], 20.0, "an untargeted channel remains untouched");
			}
		}
	}

	void checkUnderrun(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(5, 4, MixMode::Replace, {L"L"}), qpcFrequency);
		writeMono(writer, {1.0f, 2.0f, 3.0f, 4.0f}, 100);
		const AttachPlan plan = attach(region, {L"L"}, 100, 1);
		harness.require(plan.ok, "underrun reader could not attach");
		SendReaderCore reader(region.get(), plan, sampleRate, 1);
		std::array<double, 4> output = {};
		double* channels[] = {output.data()};
		harness.expect(reader.read(channels, 1, 3, 100) == SendReaderCore::Status::Delivered,
			"reader consumes the initial full block");

		writeMono(writer, {5.0f}, 200);
		std::fill(output.begin(), output.end(), -1.0);
		harness.expect(reader.read(channels, 1, 4, 200) == SendReaderCore::Status::Underrun,
			"reader reports a partial block");
		harness.expectEqual(output[0], 4.0, "partial block keeps its first available sample");
		harness.expectEqual(output[1], 5.0, "partial block keeps its second available sample");
		harness.expectEqual(output[2], 0.0, "Replace clears the first missing sample");
		harness.expectEqual(output[3], 0.0, "Replace clears the missing tail");
		harness.expectEqual(reader.underruns(), uint64_t(1), "partial block increments underruns once");

		std::fill(output.begin(), output.end(), -1.0);
		harness.expect(reader.read(channels, 1, 4, 200) == SendReaderCore::Status::Underrun,
			"reader reports a second faster block");
		harness.expect(std::all_of(output.begin(), output.end(), [](double value) { return value == 0.0; }),
			"Replace clears a wholly missing block");
		harness.expectEqual(reader.underruns(), uint64_t(2), "each short block increments underruns");
	}

	void checkLappedReader(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(6, 960, MixMode::Replace, {L"L"}), qpcFrequency);
		int64_t writePosition = 0;
		int64_t nowQpc = 0;
		for (int block = 0; block < 4; block++)
		{
			writeRamp(writer, writePosition, blockFrames, 1, nowQpc);
			writePosition += blockFrames;
			nowQpc += blockTicks;
		}
		const AttachPlan plan = attach(region, {L"L"}, nowQpc - blockTicks);
		harness.require(plan.ok, "lapped reader could not attach");
		SendReaderCore reader(region.get(), plan, sampleRate, blockFrames);

		for (int block = 0; block < 74; block++)
		{
			writeRamp(writer, writePosition, blockFrames, 1, nowQpc);
			writePosition += blockFrames;
			nowQpc += blockTicks;
		}
		std::vector<double> output(blockFrames, 0.0);
		double* channels[] = {output.data()};
		harness.expect(reader.read(channels, 1, blockFrames, nowQpc - blockTicks)
			== SendReaderCore::Status::Underrun, "a lapped reader reports an underrun");
		const int64_t expectedStart = writePosition - 960;
		bool exact = true;
		for (uint32_t frame = 0; frame < blockFrames; frame++)
			exact = exact && output[frame] == static_cast<double>(rampSample(0, expectedStart + frame));
		harness.expect(exact, "lapped reader re-anchors at write position minus latency");
		harness.expectEqual(reader.underruns(), uint64_t(1), "lap increments the underrun counter once");
	}

	enum class GoneMutation
	{
		Closing,
		Stale,
		Generation,
		SenderId
	};

	void checkGoneMutation(test::Harness& harness, GoneMutation mutation)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(7, 1, MixMode::Mix, {L"L"}), qpcFrequency);
		writeMono(writer, {2.0f}, 100);
		const AttachPlan plan = attach(region, {L"L"}, 100, 1);
		harness.require(plan.ok, "liveness reader could not attach");
		SendReaderCore reader(region.get(), plan, sampleRate, 1);
		int64_t readQpc = 100;
		switch (mutation)
		{
		case GoneMutation::Closing:
			writer.close();
			break;
		case GoneMutation::Stale:
			readQpc += qpcFrequency + 1;
			break;
		case GoneMutation::Generation:
			store32(&region.header()->generation, plan.generation + 1);
			break;
		case GoneMutation::SenderId:
			store64(&region.header()->senderId, plan.senderId + 1);
			break;
		}
		double output = 9.0;
		double* channels[] = {&output};
		harness.expect(reader.read(channels, 1, 1, readQpc) == SendReaderCore::Status::SenderGone,
			"changed sender state returns SenderGone");
		harness.expectEqual(output, 9.0, "SenderGone does not alter output");
		store32(&region.header()->state, static_cast<uint32_t>(State::Ready));
		store32(&region.header()->generation, plan.generation);
		store64(&region.header()->senderId, plan.senderId);
		harness.expect(reader.read(channels, 1, 1, 100) == SendReaderCore::Status::SenderGone,
			"SenderGone remains sticky until reattach");
	}

	void checkLiveness(test::Harness& harness)
	{
		checkGoneMutation(harness, GoneMutation::Closing);
		checkGoneMutation(harness, GoneMutation::Stale);
		checkGoneMutation(harness, GoneMutation::Generation);
		checkGoneMutation(harness, GoneMutation::SenderId);
	}

	void checkAttachRefusalsAndNames(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		SendParams valid = params(8, 960, MixMode::Mix, {L"L"});
		writer.publish(valid, qpcFrequency);

		store32(&region.header()->magic, magic + 1);
		harness.expectFalse(attach(region, {L"L"}, 0).ok, "attach refuses the wrong magic");
		store32(&region.header()->magic, magic);
		store32(&region.header()->version, layoutVersion + 1);
		harness.expectFalse(attach(region, {L"L"}, 0).ok, "attach refuses the wrong layout version");
		store32(&region.header()->version, layoutVersion);

		SendParams wrongRate = valid;
		wrongRate.sampleRate = 44'100;
		writer.publish(wrongRate, qpcFrequency);
		const AttachPlan ratePlan = attach(region, {L"L"}, 0);
		harness.expectFalse(ratePlan.ok, "attach refuses a different sample rate");
		harness.expect(ratePlan.reason == L"sample rate 44100 differs from this endpoint's 48000",
			"sample-rate refusal names both rates");

		SendParams tooLarge = valid;
		tooLarge.delayFrames = capacityFrames - 2 * blockFrames;
		writer.publish(tooLarge, qpcFrequency);
		const AttachPlan latencyPlan = attach(region, {L"L"}, 0);
		harness.expectFalse(latencyPlan.ok, "attach refuses latency at the reserved boundary");
		harness.expect(latencyPlan.reason == L"latency too large for the ring",
			"latency refusal has the pinned reason");

		writer.publish(valid, qpcFrequency);
		store32(&region.header()->channelCount, 0);
		harness.expectFalse(attach(region, {L"L"}, 0).ok, "attach refuses zero channels");
		store32(&region.header()->channelCount, 17);
		harness.expectFalse(attach(region, {L"L"}, 0).ok, "attach refuses more than sixteen channels");
		store32(&region.header()->channelCount, 1);
		store32(&region.header()->capacity, capacityFrames - 1);
		harness.expectFalse(attach(region, {L"L"}, 0).ok, "attach refuses a capacity mismatch");

		writer.publish(params(8, 960, MixMode::Mix, {L"SUB", L"XYZ"}), qpcFrequency);
		const AttachPlan names = attach(region, {L"L", L"LFE"}, 0);
		harness.require(names.ok, "alias and unknown-name reader could not attach");
		harness.expectEqual(names.targetIndex[0], 1, "SUB resolves to the receiver's LFE channel");
		harness.expectEqual(names.targetIndex[1], -1, "unknown target is ignored");
		harness.requireEqual(names.ignoredNames.size(), size_t(1), "one unknown target is listed");
		harness.expect(names.ignoredNames[0] == L"XYZ",
			"ignored-name list preserves the sender's spelling");
	}

	void checkOtherSenderLive(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(22, 1, MixMode::Mix, {L"L"}), qpcFrequency);
		writeMono(writer, {1.0f}, 1'000);
		harness.expectTrue(writer.otherSenderLive(11, 1'000, qpcFrequency),
			"fresh foreign sender is live");
		harness.expectFalse(writer.otherSenderLive(11, 1'000 + qpcFrequency + 1, qpcFrequency),
			"foreign sender older than one second is stale");
		harness.expectFalse(writer.otherSenderLive(22, 1'000, qpcFrequency),
			"the writer's own sender id is not foreign");
		writer.close();
		harness.expectFalse(writer.otherSenderLive(11, 1'000, qpcFrequency),
			"a non-Ready sender is not live");
	}

	void checkLongWriteClipsToNewestFrames(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(30, 1, MixMode::Replace, {L"L"}), qpcFrequency);
		const uint32_t inputFrames = capacityFrames + 19;
		std::vector<float> input(inputFrames);
		for (uint32_t frame = 0; frame < inputFrames; frame++)
			input[frame] = static_cast<float>(frame);
		writeMono(writer, input, 123);
		harness.expectEqual(loadSigned64(&region.header()->writePos), int64_t(capacityFrames),
			"long write clips its published cursor to one ring capacity");

		const AttachPlan plan = attach(region, {L"L"}, 123, 0);
		harness.require(plan.ok, "long-write reader could not attach");
		SendReaderCore reader(region.get(), plan, sampleRate, 0);
		double output = -1.0;
		double* channels[] = {&output};
		harness.expect(reader.read(channels, 1, 1, 123) == SendReaderCore::Status::Delivered,
			"long-write newest sample is available");
		harness.expectEqual(output, double(inputFrames - 1),
			"long write retains its newest sample");
	}

	void checkPublishCursorAndGeneration(test::Harness& harness)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		const SendParams first = params(31, 1, MixMode::Mix, {L"L"});
		writer.publish(first, qpcFrequency);
		const uint32_t generation1 = load32(&region.header()->generation);
		writeMono(writer, {1.0f, 2.0f, 3.0f, 4.0f}, 123);
		writer.publish(first, qpcFrequency);
		const uint32_t generation2 = load32(&region.header()->generation);
		harness.expectEqual(generation2, generation1 + 1, "same-sender publish increments generation");
		harness.expectEqual(loadSigned64(&region.header()->writePos), int64_t(4),
			"same-sender publish preserves write position");
		harness.expectEqual(loadSigned64(&region.header()->writeQpc), int64_t(123),
			"same-sender publish preserves the latest write time");

		writer.publish(params(32, 1, MixMode::Mix, {L"L"}), qpcFrequency);
		harness.expectEqual(load32(&region.header()->generation), generation2 + 1,
			"new-sender publish increments generation");
		harness.expectEqual(loadSigned64(&region.header()->writePos), int64_t(0),
			"new sender resets write position");
		harness.expectEqual(loadSigned64(&region.header()->writeQpc), int64_t(0),
			"new sender resets the latest write time");
	}

	uint64_t simulateDrift(int64_t writerPeriodTicks, int blocks, int64_t& finalLead)
	{
		AlignedRegion region;
		SendWriterCore writer(region.get());
		writer.publish(params(40, 1'920, MixMode::Mix, {L"L"}), qpcFrequency);
		int64_t writePosition = 0;
		int64_t nextWriterQpc = 0;
		while (nextWriterQpc <= 300'000)
		{
			writeRamp(writer, writePosition, blockFrames, 1, nextWriterQpc);
			writePosition += blockFrames;
			nextWriterQpc += writerPeriodTicks;
		}
		const AttachPlan plan = attach(region, {L"L"}, 300'000);
		if (!plan.ok)
		{
			finalLead = INT64_MAX;
			return UINT64_MAX;
		}
		SendReaderCore reader(region.get(), plan, sampleRate, blockFrames);
		std::vector<double> output(blockFrames, 0.0);
		double* channels[] = {output.data()};
		for (int block = 0; block < blocks; block++)
		{
			const int64_t readerQpc = 400'000 + int64_t(block) * blockTicks;
			while (nextWriterQpc <= readerQpc)
			{
				writeRamp(writer, writePosition, blockFrames, 1, nextWriterQpc);
				writePosition += blockFrames;
				nextWriterQpc += writerPeriodTicks;
			}
			reader.read(channels, 1, blockFrames, readerQpc);
		}
		finalLead = loadSigned64(&region.header()->writePos)
			- loadSigned64(&region.header()->readPos);
		return reader.driftSteps();
	}

	void checkDrift(test::Harness& harness)
	{
		int64_t equalLead = 0;
		const uint64_t equalSteps = simulateDrift(blockTicks, 10'000, equalLead);
		harness.expectEqual(equalSteps, uint64_t(0), "equal clocks take no drift steps over 10000 blocks");

		int64_t fastLead = 0;
		const uint64_t fastSteps = simulateDrift(99'990, 12'000, fastLead);
		harness.expect(fastSteps > 0 && fastSteps != UINT64_MAX,
			"a writer clock 100 ppm fast causes drift correction");
		const int64_t leadAtAttach = 5 * int64_t(blockFrames);
		harness.expect(fastLead >= leadAtAttach - blockFrames && fastLead <= leadAtAttach + blockFrames,
			"drift correction keeps lead within one maximum block of its attach average");
	}

	void checkWin32Adapter(test::Harness& harness)
	{
		const std::wstring guid = L"{01234567-89ab-cdef-0123-456789abcdef}";
		const std::wstring prefix = L"Local\\EAPOTest." + std::to_wstring(GetCurrentProcessId())
			+ L".SendRing.";
		const std::wstring map = mappingName(prefix, guid);
		const std::wstring ready = eventName(prefix, guid);
		SendRingWin32 first(map, ready);
		first.touchAllPages();
		SendRingWin32 second(map, ready);
		store32(&static_cast<Header*>(first.region())->magic, magic);
		harness.expectEqual(load32(&static_cast<Header*>(second.region())->magic), magic,
			"second mapping view sees the first view's header write");
		harness.expect(first.signal(), "adapter signals its manual-reset event");
		harness.expectEqual(WaitForSingleObject(second.readyEvent(), 0), DWORD(WAIT_OBJECT_0),
			"second event handle observes the signal");
		harness.expect(ResetEvent(second.readyEvent()) != FALSE, "manual-reset event resets");
		harness.expectEqual(WaitForSingleObject(first.readyEvent(), 0), DWORD(WAIT_TIMEOUT),
			"first event handle observes the reset");
	}
}

void runSendRingTests(test::Harness& harness)
{
	checkLayoutAndNames(harness);
	checkDelayedReadWithPhase(harness);
	checkWrap(harness);
	checkMixAndReplace(harness);
	checkUnderrun(harness);
	checkLappedReader(harness);
	checkLiveness(harness);
	checkAttachRefusalsAndNames(harness);
	checkOtherSenderLive(harness);
	checkLongWriteClipsToNewestFrames(harness);
	checkPublishCursorAndGeneration(harness);
	checkDrift(harness);
	checkWin32Adapter(harness);
}
