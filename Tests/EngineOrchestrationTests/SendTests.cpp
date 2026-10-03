/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/ConfigLoadTrace.h"
#include "runtime/ipc/SendRing.h"
#include "services/logging/Logging.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
	using eapo::ipc::send::Header;
	using eapo::ipc::send::mappingName;
	using eapo::ipc::send::regionBytes;

	constexpr unsigned sampleRate = 48'000;
	constexpr unsigned blockFrames = 480;
	const std::wstring senderGuid = L"{11111111-1111-1111-1111-111111111111}";
	const std::wstring receiverGuid = L"{22222222-2222-2222-2222-222222222222}";
	const std::wstring thirdGuid = L"{33333333-3333-3333-3333-333333333333}";
	std::atomic<unsigned> testSerial{1};

	struct TraceCollector : ConfigLoadTraceSink
	{
		std::vector<ConfigLoadTraceEntry> entries;

		void addEntry(const ConfigLoadTraceEntry& entry) override
		{
			entries.push_back(entry);
		}

		bool hasErrorContaining(const std::wstring& text) const
		{
			for (const ConfigLoadTraceEntry& entry : entries)
			{
				if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError
					&& entry.text.find(text) != std::wstring::npos)
				{
					return true;
				}
			}
			return false;
		}

		size_t parseErrorCount() const
		{
			return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
				[](const ConfigLoadTraceEntry& entry) {
					return entry.kind == ConfigLoadTraceEntry::Kind::ParseError;
				}));
		}
	};

	class RingView
	{
	public:
		explicit RingView(const std::wstring& name)
			: mapping_(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()))
		{
			if (mapping_ != nullptr)
				view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, regionBytes);
		}

		~RingView()
		{
			if (view_ != nullptr)
				UnmapViewOfFile(view_);
			if (mapping_ != nullptr)
				CloseHandle(mapping_);
		}

		RingView(const RingView&) = delete;
		RingView& operator=(const RingView&) = delete;

		bool valid() const noexcept {return view_ != nullptr;}
		const Header* header() const noexcept {return static_cast<const Header*>(view_);}

	private:
		HANDLE mapping_ = nullptr;
		void* view_ = nullptr;
	};

	std::wstring prefix(const wchar_t* label)
	{
		return L"Local\\EAPOTest." + std::to_wstring(GetCurrentProcessId()) + L"."
			+ std::to_wstring(testSerial.fetch_add(1)) + L"." + label + L".Send.";
	}

	std::string narrowGuid(const std::wstring& guid)
	{
		return toNarrow(guid);
	}

	EngineSetup apoSetup(const std::wstring& configPath, const std::wstring& guid,
		const std::wstring& sendPrefix, unsigned rate = sampleRate)
	{
		EngineSetup setup = testEngineSetup(rate, 2, 2, blockFrames, configPath);
		setup.host = EngineHost::Apo;
		setup.deviceGuid = guid;
		setup.sendNamePrefix = sendPrefix;
		setup.preMix = false;
		setup.capture = false;
		setup.postMixInstalled = true;
		return setup;
	}

	std::vector<float> stereoBlock(float left, float right)
	{
		std::vector<float> block(static_cast<size_t>(blockFrames) * 2);
		for (unsigned frame = 0; frame < blockFrames; frame++)
		{
			block[static_cast<size_t>(frame) * 2] = left;
			block[static_cast<size_t>(frame) * 2 + 1] = right;
		}
		return block;
	}

	std::vector<float> rampBlock(int64_t firstPosition, bool stereoAverage)
	{
		std::vector<float> block(static_cast<size_t>(blockFrames) * 2);
		for (unsigned frame = 0; frame < blockFrames; frame++)
		{
			const float position = static_cast<float>(firstPosition + frame);
			block[static_cast<size_t>(frame) * 2] = position;
			block[static_cast<size_t>(frame) * 2 + 1] = stereoAverage ? position + 2.0f : 0.0f;
		}
		return block;
	}

	std::vector<float> processBlock(FilterEngine& engine, std::vector<float> input)
	{
		std::vector<float> output(input.size(), 0.0f);
		engine.process(output.data(), input.data(), blockFrames);
		return output;
	}

	void settle(FilterEngine& engine)
	{
		processBlock(engine, stereoBlock(0.0f, 0.0f));
	}

	int64_t load64(const volatile LONG64* value)
	{
		return static_cast<int64_t>(ReadAcquire64(value));
	}

	uint32_t load32(const volatile LONG* value)
	{
		return static_cast<uint32_t>(ReadAcquire(value));
	}

	std::wstring readLog(FILE* stream)
	{
		std::fflush(stream);
		std::rewind(stream);
		std::wstring result;
		wchar_t buffer[1024] = {};
		while (std::fgetws(buffer, static_cast<int>(std::size(buffer)), stream) != nullptr)
			result.append(buffer);
		return result;
	}

	void checkAttachBeforeFirstProcessAndIdleResume(test::Harness& harness)
	{
		const std::wstring sendPrefix = prefix(L"late-start-idle-resume");
		const std::wstring senderPath = writeConfig(harness, L"send-late-start-sender.txt",
			"Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=false Latency=480samples\n");
		const std::wstring receiverPath = writeConfig(harness, L"send-late-start-receiver.txt", "# empty\n");

		FilterEngine sender;
		sender.initialize(apoSetup(senderPath, senderGuid, sendPrefix));
		FilterEngine receiver;
		receiver.initialize(apoSetup(receiverPath, receiverGuid, sendPrefix));
		harness.expectTrue(receiver.hasStatefulOrTailFilters(),
			"the receiver attaches before the sender's first process call");

		const std::vector<float> beforeFirstWrite = processBlock(receiver, stereoBlock(1.0f, 4.0f));
		harness.expectEqual(beforeFirstWrite[0], 1.0f,
			"an attached sender that has not written leaves receiver audio unchanged");
		bool delivered = false;
		for (int block = 0; block < 3; block++)
		{
			processBlock(sender, stereoBlock(2.0f, 0.0f));
			const std::vector<float> output = processBlock(receiver, stereoBlock(1.0f, 4.0f));
			delivered = output[0] == 3.0f && output[1] == 4.0f;
		}
		harness.expect(delivered,
			"a receiver attached before the first sender block delivers after interleaved processing");

		Sleep(1'100);
		const std::vector<float> idle = processBlock(receiver, stereoBlock(1.0f, 4.0f));
		harness.expectEqual(idle[0], 1.0f, "an idle sender contributes no stale audio");
		harness.expectTrue(receiver.hasStatefulOrTailFilters(),
			"an idle attachment keeps the receiver on the audio path");

		processBlock(sender, stereoBlock(5.0f, 0.0f));
		const std::vector<float> resumed = processBlock(receiver, stereoBlock(1.0f, 4.0f));
		harness.expectEqual(resumed[0], 6.0f,
			"the sender resumes after more than one second without a receiver reload");
		harness.expectEqual(resumed[1], 4.0f, "idle recovery leaves the untargeted channel unchanged");
	}

	void checkRoutingAndReceiverState(test::Harness& harness)
	{
		const std::wstring sendPrefix = prefix(L"routing");
		const std::string guid = narrowGuid(receiverGuid);
		const std::wstring receiverPath = writeConfig(harness, L"send-routing-receiver.txt", "# empty\n");
		const std::wstring senderPath = writeConfig(harness, L"send-routing-sender.txt",
			"NewChannel: SUB\n"
			"Copy: SUB=0.5*L+0.5*R\n"
			"Send: " + guid + " L=SUB Compensate=false Latency=960samples\n");

		FilterEngine receiver;
		receiver.initialize(apoSetup(receiverPath, receiverGuid, sendPrefix));
		settle(receiver);
		FilterEngine sender;
		sender.initialize(apoSetup(senderPath, senderGuid, sendPrefix));

		int64_t senderPosition = 0;
		for (int block = 0; block < 4; block++)
		{
			processBlock(sender, rampBlock(senderPosition, true));
			senderPosition += blockFrames;
		}

		RingView ring(mappingName(sendPrefix, receiverGuid));
		harness.require(ring.valid(), "routing test could not open the Send ring");
		harness.expectEqual(load32(&ring.header()->delayFrames), uint32_t(960),
			"the published Send latency is exactly 960 frames");
		// A customPath engine has no watcher. The sender has published and written,
		// so ask the receiver to load once more and attach to the ready ring.
		harness.require(receiver.loadConfig(receiverPath), "receiver could not reload to attach");
		const int64_t firstReadPosition = load64(&ring.header()->readPos);
		harness.require(firstReadPosition >= 0 && firstReadPosition + blockFrames <= senderPosition,
			"receiver attached outside the ramp already present in the ring");
		harness.expectTrue(receiver.hasStatefulOrTailFilters(),
			"an attached receiver keeps an empty configuration on the audio path");

		processBlock(sender, rampBlock(senderPosition, true));
		std::vector<float> receiverInput = stereoBlock(0.0f, -123.0f);
		const std::vector<float> received = processBlock(receiver, std::move(receiverInput));
		bool exact = true;
		bool rightUntouched = true;
		for (unsigned frame = 0; frame < blockFrames; frame++)
		{
			const float expected = static_cast<float>(firstReadPosition + frame + 1);
			exact = exact && received[static_cast<size_t>(frame) * 2] == expected;
			rightUntouched = rightUntouched
				&& received[static_cast<size_t>(frame) * 2 + 1] == -123.0f;
		}
		harness.expect(exact,
			"the receiver gets the sample-exact SUB ramp from its recorded attach cursor");
		harness.expect(rightUntouched, "the untargeted receiver R channel passes through unchanged");
		harness.expectEqual(load64(&ring.header()->readPos), firstReadPosition + blockFrames,
			"the receiver advances its cursor by one block");
	}

	std::vector<float> runCompensation(test::Harness& harness, bool compensate,
		const wchar_t* label)
	{
		const std::wstring sendPrefix = prefix(label);
		const std::string line = "Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=" + (compensate ? "true" : "false")
			+ " Latency=960samples\n";
		const std::wstring path = writeConfig(harness,
			compensate ? L"send-compensate-true.txt" : L"send-compensate-false.txt", line);
		FilterEngine engine;
		engine.initialize(apoSetup(path, senderGuid, sendPrefix));
		std::vector<float> result(static_cast<size_t>(blockFrames) * 3 * 2, 0.0f);
		for (int block = 0; block < 3; block++)
		{
			std::vector<float> input = stereoBlock(0.0f, 0.0f);
			if (block == 0)
			{
				input[0] = 1.0f;
				input[1] = 0.5f;
			}
			const std::vector<float> output = processBlock(engine, std::move(input));
			std::copy(output.begin(), output.end(), result.begin()
				+ static_cast<size_t>(block) * blockFrames * 2);
		}
		return result;
	}

	void checkCompensation(test::Harness& harness)
	{
		const std::vector<float> delayed = runCompensation(harness, true, L"comp-true");
		const std::vector<float> immediate = runCompensation(harness, false, L"comp-false");
		harness.expectEqual(immediate[0], 1.0f,
			"Compensate=false leaves the sender L output at its original time");
		harness.expectEqual(immediate[1], 0.5f,
			"Compensate=false leaves the sender R output at its original time");
		harness.expectEqual(delayed[0], 0.0f,
			"Compensate=true removes the impulse from the sender's first frame");
		const size_t delayedFrame = static_cast<size_t>(960) * 2;
		harness.expectEqual(delayed[delayedFrame], 1.0f,
			"Compensate=true delays sender L by exactly D");
		harness.expectEqual(delayed[delayedFrame + 1], 0.5f,
			"Compensate=true delays sender R by exactly D");
	}

	void checkMixMode(test::Harness& harness, const std::string& mode,
		float expectedLeft, const wchar_t* label)
	{
		const std::wstring sendPrefix = prefix(label);
		const std::wstring receiverPath = writeConfig(harness,
			mode == "Mix" ? L"send-mix-receiver.txt" : L"send-replace-receiver.txt", "# empty\n");
		const std::wstring senderPath = writeConfig(harness,
			mode == "Mix" ? L"send-mix-sender.txt" : L"send-replace-sender.txt",
			"Send: " + narrowGuid(receiverGuid) + " L=L Compensate=false Latency=960samples Mode="
			+ mode + "\n");
		FilterEngine receiver;
		receiver.initialize(apoSetup(receiverPath, receiverGuid, sendPrefix));
		settle(receiver);
		FilterEngine sender;
		sender.initialize(apoSetup(senderPath, senderGuid, sendPrefix));
		for (int block = 0; block < 3; block++)
			processBlock(sender, stereoBlock(2.0f, 0.0f));
		harness.require(receiver.loadConfig(receiverPath), "mode receiver could not attach");
		processBlock(sender, stereoBlock(2.0f, 0.0f));
		const std::vector<float> output = processBlock(receiver, stereoBlock(10.0f, 20.0f));
		bool correct = true;
		for (unsigned frame = 0; frame < blockFrames; frame++)
		{
			correct = correct && output[static_cast<size_t>(frame) * 2] == expectedLeft
				&& output[static_cast<size_t>(frame) * 2 + 1] == 20.0f;
		}
		harness.expect(correct, mode == "Mix"
			? "Mode=Mix adds Send audio and leaves R alone"
			: "Mode=Replace overwrites L and leaves R alone");
	}

	void checkCrossfadeDeduplication(test::Harness& harness)
	{
		const std::wstring sendPrefix = prefix(L"crossfade");
		const std::string sendLine = "Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=false Latency=960samples\n";
		const std::wstring receiverPath = writeConfig(harness, L"send-crossfade-receiver.txt", "# empty\n");
		const std::wstring senderAPath = writeConfig(harness, L"send-crossfade-sender-a.txt",
			sendLine + "Preamp: 0 dB\n");
		const std::wstring senderBPath = writeConfig(harness, L"send-crossfade-sender-b.txt",
			sendLine + "Preamp: -6.0206 dB\n");

		FilterEngine receiver;
		receiver.initialize(apoSetup(receiverPath, receiverGuid, sendPrefix));
		settle(receiver);
		FilterEngine sender;
		sender.initialize(apoSetup(senderAPath, senderGuid, sendPrefix));
		int64_t senderPosition = 0;
		for (int block = 0; block < 4; block++)
		{
			processBlock(sender, rampBlock(senderPosition, false));
			senderPosition += blockFrames;
		}
		RingView ring(mappingName(sendPrefix, receiverGuid));
		harness.require(ring.valid(), "crossfade test could not open the Send ring");
		harness.require(receiver.loadConfig(receiverPath), "crossfade receiver could not attach");
		processBlock(sender, rampBlock(senderPosition, false));
		senderPosition += blockFrames;
		processBlock(receiver, stereoBlock(0.0f, 0.0f));

		harness.require(sender.loadConfig(senderBPath), "sender crossfade configuration did not load");
		// customPath disables the event watcher, so this explicit reload is the
		// receiver-side equivalent of the ready-event reload in an APO instance.
		harness.require(receiver.loadConfig(receiverPath), "receiver crossfade configuration did not load");
		const int64_t writeBefore = load64(&ring.header()->writePos);
		const int64_t readBefore = load64(&ring.header()->readPos);
		processBlock(sender, rampBlock(senderPosition, false));
		const int64_t writeAfter = load64(&ring.header()->writePos);
		const std::vector<float> output = processBlock(receiver, stereoBlock(0.0f, 0.0f));
		const int64_t readAfter = load64(&ring.header()->readPos);

		harness.expectEqual(writeAfter - writeBefore, int64_t(blockFrames),
			"two sender configurations write the ring once per process block");
		harness.expectEqual(readAfter - readBefore, int64_t(blockFrames),
			"two receiver configurations read the ring once per process block");
		bool exact = true;
		for (unsigned frame = 0; frame < blockFrames; frame++)
		{
			exact = exact && output[static_cast<size_t>(frame) * 2]
				== static_cast<float>(readBefore + frame);
		}
		harness.expect(exact,
			"receiver crossfade reuses one scratch block without doubling or skipping the ramp");
	}

	void checkSampleRateAndSenderDeparture(test::Harness& harness)
	{
		const std::wstring mismatchPrefix = prefix(L"rate");
		const std::wstring receiverPath = writeConfig(harness, L"send-rate-receiver.txt", "# empty\n");
		const std::wstring senderPath = writeConfig(harness, L"send-rate-sender.txt",
			"Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=false Latency=960samples\n");
		FilterEngine receiver;
		receiver.initialize(apoSetup(receiverPath, receiverGuid, mismatchPrefix, 44'100));
		settle(receiver);
		FilterEngine sender;
		sender.initialize(apoSetup(senderPath, senderGuid, mismatchPrefix));
		processBlock(sender, stereoBlock(3.0f, 0.0f));

		FILE* logFile = nullptr;
		harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr,
			"sample-rate log capture is available");
		Logging::useStream(logFile, true, true, false);
		harness.require(receiver.loadConfig(receiverPath), "mismatched receiver reload failed");
		const std::wstring log = readLog(logFile);
		std::fclose(logFile);
		Logging::useStream(stderr, false, false, false);
		const std::vector<float> mismatchOutput = processBlock(receiver, stereoBlock(7.0f, 8.0f));
		harness.expect(log.find(L"sample rate 48000 differs from this endpoint's 44100")
			!= std::wstring::npos, "sample-rate refusal logs both endpoint rates");
		harness.expectEqual(mismatchOutput[0], 7.0f,
			"sample-rate mismatch leaves receiver input unchanged");
		harness.expectFalse(receiver.hasStatefulOrTailFilters(),
			"sample-rate mismatch does not claim that the empty receiver may add audio");

		const std::wstring gonePrefix = prefix(L"gone");
		const std::wstring goneReceiverPath = writeConfig(harness, L"send-gone-receiver.txt", "# empty\n");
		const std::wstring goneSenderPath = writeConfig(harness, L"send-gone-sender.txt",
			"Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=false Latency=960samples\n");
		FilterEngine goneReceiver;
		goneReceiver.initialize(apoSetup(goneReceiverPath, receiverGuid, gonePrefix));
		settle(goneReceiver);
		auto goneSender = std::make_unique<FilterEngine>();
		goneSender->initialize(apoSetup(goneSenderPath, senderGuid, gonePrefix));
		for (int block = 0; block < 3; block++)
			processBlock(*goneSender, stereoBlock(2.0f, 0.0f));
		harness.require(goneReceiver.loadConfig(goneReceiverPath), "gone receiver could not attach");
		processBlock(*goneSender, stereoBlock(2.0f, 0.0f));
		const std::vector<float> live = processBlock(goneReceiver, stereoBlock(1.0f, 4.0f));
		harness.expectEqual(live[0], 3.0f, "live sender contributes to the receiver before destruction");
		goneSender.reset();
		const std::vector<float> gone = processBlock(goneReceiver, stereoBlock(1.0f, 4.0f));
		harness.expectEqual(gone[0], 1.0f, "Closing sender adds nothing on the next receiver block");
		harness.expectFalse(goneReceiver.hasStatefulOrTailFilters(),
			"SenderGone makes the receiver tap report that it cannot add audio");
	}

	void checkSecondSenderRefusal(test::Harness& harness)
	{
		const std::wstring sendPrefix = prefix(L"second-sender");
		const std::string line = "Send: " + narrowGuid(receiverGuid)
			+ " L=L Compensate=false Latency=960samples\n";
		const std::wstring firstPath = writeConfig(harness, L"send-first-sender.txt", line);
		const std::wstring secondPath = writeConfig(harness, L"send-second-sender.txt", line);
		FilterEngine first;
		first.initialize(apoSetup(firstPath, senderGuid, sendPrefix));
		processBlock(first, stereoBlock(2.0f, 0.0f));

		TraceCollector trace;
		FilterEngine second;
		second.setLoadTraceSink(&trace);
		second.initialize(apoSetup(secondPath, thirdGuid, sendPrefix));
		harness.expect(trace.hasErrorContaining(L"Another sender already feeds " + receiverGuid),
			"a fresh foreign sender makes the second engine refuse its Send line");
	}

	void checkRefusalsAndInactiveHost(test::Harness& harness)
	{
		const std::string target = narrowGuid(receiverGuid);

		const std::wstring selfPrefix = prefix(L"self");
		const std::wstring selfPath = writeConfig(harness, L"send-self.txt",
			"Send: " + target + " L=L\n");
		TraceCollector selfTrace;
		FilterEngine self;
		self.setLoadTraceSink(&selfTrace);
		self.initialize(apoSetup(selfPath, receiverGuid, selfPrefix));
		harness.expect(selfTrace.hasErrorContaining(L"Send cannot target its own endpoint"),
			"self-targeting Send is reported on its configuration line");

		const std::wstring unknownPrefix = prefix(L"unknown");
		const std::wstring unknownPath = writeConfig(harness, L"send-unknown-source.txt",
			"Send: " + target
			+ " L=NOPE R=L Compensate=false Latency=960samples\n");
		TraceCollector unknownTrace;
		FilterEngine unknown;
		unknown.setLoadTraceSink(&unknownTrace);
		unknown.initialize(apoSetup(unknownPath, senderGuid, unknownPrefix));
		RingView unknownRing(mappingName(unknownPrefix, receiverGuid));
		harness.require(unknownRing.valid(), "remaining valid assignment did not create a Send ring");
		harness.expect(unknownTrace.hasErrorContaining(L"Send source channel NOPE does not exist here"),
			"unknown Send source names the dropped assignment");
		harness.expectEqual(load32(&unknownRing.header()->channelCount), uint32_t(1),
			"a valid assignment remains after the unknown assignment is dropped");
		harness.expect(std::wstring(unknownRing.header()->targetNames[0]) == L"R",
			"the remaining assignment publishes its target name");

		const std::wstring testPrefix = prefix(L"inactive");
		const std::wstring testPath = writeConfig(harness, L"send-inactive-host.txt",
			"Send: " + target + " L=L\n");
		TraceCollector testTrace;
		FilterEngine testHost;
		testHost.setLoadTraceSink(&testTrace);
		EngineSetup testSetup = apoSetup(testPath, senderGuid, testPrefix);
		testSetup.host = EngineHost::Test;
		testHost.initialize(testSetup);
		harness.expectEqual(testTrace.parseErrorCount(), size_t(0),
			"Send outside the audio service is inactive rather than erroneous");
		RingView absent(mappingName(testPrefix, receiverGuid));
		harness.expectFalse(absent.valid(), "inactive Test host creates no Send mapping");

		const std::wstring preMixPrefix = prefix(L"premix");
		const std::wstring preMixPath = writeConfig(harness, L"send-premix.txt",
			"Stage: pre-mix\nSend: " + target + " L=L\n");
		TraceCollector preMixTrace;
		FilterEngine preMix;
		preMix.setLoadTraceSink(&preMixTrace);
		EngineSetup preMixSetup = apoSetup(preMixPath, senderGuid, preMixPrefix);
		preMixSetup.preMix = true;
		preMix.initialize(preMixSetup);
		harness.expect(preMixTrace.hasErrorContaining(
			L"Send requires the post-mix stage to be installed on this endpoint"),
			"a pre-mix engine refuses Send on its configuration line");

		const std::wstring capturePrefix = prefix(L"capture");
		const std::wstring capturePath = writeConfig(harness, L"send-capture.txt",
			"Stage: capture\nSend: " + target + " L=L\n");
		TraceCollector captureTrace;
		FilterEngine capture;
		capture.setLoadTraceSink(&captureTrace);
		EngineSetup captureSetup = apoSetup(capturePath, senderGuid, capturePrefix);
		captureSetup.capture = true;
		capture.initialize(captureSetup);
		harness.expect(captureTrace.hasErrorContaining(L"Send is not available on recording endpoints"),
			"a recording endpoint refuses Send on its configuration line");

		const std::wstring recoveredPrefix = prefix(L"recovered");
		const std::wstring recoveredPath = writeConfig(harness, L"send-invalid-then-valid.txt",
			"Send: " + target + " L=NOPE Compensate=false\n"
			"Send: " + target + " R=R Compensate=false\n");
		TraceCollector recoveredTrace;
		FilterEngine recovered;
		recovered.setLoadTraceSink(&recoveredTrace);
		recovered.initialize(apoSetup(recoveredPath, senderGuid, recoveredPrefix));
		RingView recoveredRing(mappingName(recoveredPrefix, receiverGuid));
		harness.require(recoveredRing.valid(),
			"a fully dropped Send line does not reserve its target for the next line");
		harness.expectEqual(load32(&recoveredRing.header()->channelCount), uint32_t(1),
			"the valid line after a fully dropped line publishes one channel");

		const std::wstring duplicatePrefix = prefix(L"duplicate");
		const std::wstring duplicatePath = writeConfig(harness, L"send-duplicate-target.txt",
			"Send: " + target + " L=L Compensate=false\n"
			"Send: " + target + " R=R Compensate=false\n");
		TraceCollector duplicateTrace;
		FilterEngine duplicate;
		duplicate.setLoadTraceSink(&duplicateTrace);
		duplicate.initialize(apoSetup(duplicatePath, senderGuid, duplicatePrefix));
		harness.expect(duplicateTrace.hasErrorContaining(
			L"Another Send line in this configuration already feeds " + receiverGuid),
			"a second Send line to one target is refused");

		const std::wstring latencyPrefix = prefix(L"latency");
		const std::wstring latencyPath = writeConfig(harness, L"send-latency-too-large.txt",
			"Send: " + target + " L=L Latency=31808samples\n");
		TraceCollector latencyTrace;
		FilterEngine latency;
		latency.setLoadTraceSink(&latencyTrace);
		latency.initialize(apoSetup(latencyPath, senderGuid, latencyPrefix));
		harness.expect(latencyTrace.hasErrorContaining(
			L"Latency of 31808 frames is too large for the Send ring"),
			"latency at the reserved ring boundary is refused");
	}
}

void runSendTests(test::Harness& harness)
{
	checkAttachBeforeFirstProcessAndIdleResume(harness);
	checkRoutingAndReceiverState(harness);
	checkCompensation(harness);
	checkMixMode(harness, "Mix", 12.0f, L"mix");
	checkMixMode(harness, "Replace", 2.0f, L"replace");
	checkCrossfadeDeduplication(harness);
	checkSampleRateAndSenderDeparture(harness);
	checkSecondSenderRefusal(harness);
	checkRefusalsAndInactiveHost(harness);
}
