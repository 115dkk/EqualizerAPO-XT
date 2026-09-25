/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The second kind of target behind the ASIO wrapper
	(docs/architecture/wasapi-exclusive-study.md, section 5): an IASIO that
	opens Windows audio endpoints in WASAPI exclusive mode instead of
	forwarding to a hardware ASIO driver. An application that picks the
	wrapper entry gets what exclusive mode gives it - no engine mixing or
	resampling, the device's own period, the sample rate it asks for - and
	the engine host processes the stream on the way, exactly as for a real
	ASIO target. The wrapper does not know the difference.

	One target holds up to two endpoints of one device: a playback endpoint
	(the ASIO outputs) and a recording endpoint (the ASIO inputs). Both run
	event driven at the ASIO buffer size; when both are present the playback
	stream is the clock and captured packets are queued into the input
	buffers, zero-filled when the recording side has not delivered yet.

	Exclusive mode takes integer containers on most hardware, so the sample
	type reported per channel is whatever the endpoint accepted at the
	current rate, tried in a fixed order from the endpoint's own device
	format. The wrapper converts to float at its edge as it does for any
	driver. Nothing here depends on the Common library: the file compiles
	into the wrapper DLL, the probe and the tests alike.

	The pure parts (container order, buffer-size policy, interleaving, the
	bridge calibration, the capture queue, the output staging and the
	judgement of a device that went away) live in the wasapi namespace so
	the tests can pin them without a device; the stream thread only calls
	COM and hands them the numbers.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "asio/AsioSdk.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace eapo::asio
{
	namespace wasapi
	{
		// A sample container exclusive mode may accept, with the ASIO type the
		// wrapper sees for it.
		struct Container
		{
			long asioType = 0;          // ASIOSampleType
			unsigned bits = 0;          // container bits
			unsigned validBits = 0;     // valid bits inside the container
			bool isFloat = false;

			unsigned bytes() const noexcept {return bits / 8;}
		};

		// The containers in the order they are tried at a sample rate: the
		// endpoint's own device format first (that is what the Sound
		// settings show and what an exclusive-mode player would match), then
		// float, 32, 24-in-32, packed 24 and 16 bit. An unknown device
		// format contributes nothing and the fixed list stands.
		std::vector<Container> containerCandidates(const WAVEFORMATEX* deviceFormat);

		// The WAVEFORMATEXTENSIBLE for a container at a rate and layout.
		WAVEFORMATEXTENSIBLE makeFormat(const Container& container, unsigned channels, unsigned rate, unsigned long channelMask);

		// What getBufferSize answers for an endpoint whose smallest exclusive
		// period is `minPeriodFrames`: powers of two from the first one at or
		// above the minimum (and never below 32) up to 2048, the smallest
		// preferred. Powers of two satisfy every alignment a WaveRT driver
		// has been seen to demand (HDAudio wants multiples of 128).
		struct BufferPolicy
		{
			long minSize = 0;
			long maxSize = 0;
			long preferredSize = 0;
			long granularity = -1;
		};
		BufferPolicy bufferPolicy(unsigned minPeriodFrames);

		struct CapturePlan
		{
			size_t dropFromQueue = 0;
			size_t copyFrames = 0;
		};
		CapturePlan planCapturePacket(size_t pendingFrames, size_t capacityFrames, size_t packetFrames) noexcept;

		// Frames <-> 100 ns units at a rate, both to nearest, so a period
		// survives the round trip the audio stack puts it through.
		unsigned framesFromHns(long long hns, unsigned rate);
		long long hnsFromFrames(unsigned frames, unsigned rate);

		// Planes of `bytesPerSample` samples <-> one interleaved block.
		void interleave(const void* const* planes, unsigned channels, unsigned bytesPerSample, unsigned frames, void* block);
		void deinterleave(const void* block, unsigned channels, unsigned bytesPerSample, unsigned frames, void* const* planes);

		// The MMDevice id of an endpoint GUID ({...}) in one flow.
		std::wstring endpointId(bool capture, const std::wstring& endpointGuid);

		// The bridge calibration (f43d797d, docs/features/asio.md): the
		// spacing of the first events of a stream, their median against the
		// ASIO period, a device period of the smallest multiple that covers
		// it when the median is over one and a half periods, never more than
		// eight. The values are measured; change them here only.
		constexpr unsigned bridgeCalibrationEvents = 12;
		constexpr unsigned bridgeCap = 8;
		constexpr uint64_t bridgeThresholdNumerator = 3;     // 1.5 periods
		constexpr uint64_t bridgeThresholdDenominator = 2;

		// Decides how many ASIO periods one device period holds. Fed the
		// spacing of consecutive device events; decides once, on the last
		// calibration event. A forced value (EAPO_WASAPI_FORCE_BRIDGE, read
		// by the caller) between 2 and the cap decides up front; any other
		// forced value is ignored.
		class BridgeCalibrator
		{
		public:
			BridgeCalibrator(uint64_t periodNanos, int forcedBridge) noexcept;

			// True on the call that decides; spacings after that are ignored.
			bool addSpacing(uint64_t spacingNanos) noexcept;
			bool decided() const noexcept {return decided_;}
			bool forced() const noexcept {return forced_;}
			unsigned factor() const noexcept {return factor_;}

		private:
			uint64_t periodNanos_ = 0;
			uint64_t spacings_[bridgeCalibrationEvents] = {};
			unsigned count_ = 0;
			unsigned factor_ = 1;
			bool decided_ = false;
			bool forced_ = false;
		};

		// The recording side's frames between the device's packets and the
		// ASIO periods. Holds at most `capacityFrames` (two device periods);
		// older audio makes way so the input never drifts further than that
		// behind the output clock.
		class CaptureQueue
		{
		public:
			void reset(unsigned channels, unsigned bytesPerSample, size_t capacityFrames);
			void clear() noexcept;

			// One captured packet of interleaved frames; a silent packet
			// (AUDCLNT_BUFFERFLAGS_SILENT) queues zeros and `data` is not read.
			void push(const void* data, size_t frames, bool silent) noexcept;
			// One ASIO period of `frames` into the planes. When fewer frames
			// are queued the planes are zero-filled, the queue is left as it
			// is, an underrun is counted and the answer is false.
			bool take(void* const* planes, unsigned frames) noexcept;

			size_t pendingFrames() const noexcept {return pendingFrames_;}
			size_t capacityFrames() const noexcept {return frameBytes_ != 0 ? storage_.size() / frameBytes_ : 0;}
			uint64_t underruns() const noexcept {return underruns_;}

		private:
			std::vector<unsigned char> storage_;
			unsigned channels_ = 0;
			unsigned bytesPerSample_ = 0;
			size_t frameBytes_ = 0;
			size_t pendingFrames_ = 0;
			uint64_t underruns_ = 0;
		};

		// The playback side's ASIO periods gathered into one device period:
		// `bridge` periods, each at its slot of the device block, written to
		// the device in one GetBuffer/ReleaseBuffer once the last is in.
		class OutputStager
		{
		public:
			struct Step
			{
				unsigned slot = 0;          // where this ASIO period goes in the block
				unsigned writeFrames = 0;   // frames to write to the device now; 0 while gathering
			};

			void reset(unsigned bridge, unsigned framesPerPeriod) noexcept;
			Step stage() noexcept;
			unsigned bridge() const noexcept {return bridge_;}
			unsigned staged() const noexcept {return staged_;}

		private:
			unsigned bridge_ = 1;
			unsigned framesPerPeriod_ = 0;
			unsigned staged_ = 0;
		};

		// A device that went away: AUDCLNT_E_DEVICE_INVALIDATED from a
		// GetBuffer or ReleaseBuffer, or this many event waits of
		// `eventWaitMs` in a row without an event.
		constexpr unsigned long eventWaitMs = 500;
		constexpr unsigned deviceLostTimeouts = 4;

		enum class StreamHealth
		{
			Continue,       // an event came; serve on
			Retry,          // the wait timed out; wait again
			DeviceLost,     // end the stream and ask the host for a reset
		};

		class StreamHealthJudge
		{
		public:
			// After each wait: whether it timed out, and the worst HRESULT the
			// device calls of that event returned (S_OK after a timeout). An
			// event resets the timeout count.
			StreamHealth judgeWait(bool timedOut, HRESULT deviceResult) noexcept;
			unsigned consecutiveTimeouts() const noexcept {return timeouts_;}

		private:
			unsigned timeouts_ = 0;
		};
	}

	class WasapiExclusiveTarget final : public IASIO
	{
	public:
		// Either GUID may be empty; not both. The GUIDs are the endpoint
		// GUIDs the Device Selector knows ({...}), not full MMDevice ids.
		WasapiExclusiveTarget(std::wstring renderEndpointGuid, std::wstring captureEndpointGuid);
		~WasapiExclusiveTarget();
		WasapiExclusiveTarget(const WasapiExclusiveTarget&) = delete;
		WasapiExclusiveTarget& operator=(const WasapiExclusiveTarget&) = delete;

		// IUnknown
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		// IASIO
		ASIOBool init(void* sysHandle) override;
		void getDriverName(char* name) override;
		long getDriverVersion() override;
		void getErrorMessage(char* string) override;
		ASIOError start() override;
		ASIOError stop() override;
		ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
		ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
		ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
		ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
		ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
		ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
		ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
		ASIOError setClockSource(long reference) override;
		ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override;
		ASIOError getChannelInfo(ASIOChannelInfo* info) override;
		ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override;
		ASIOError disposeBuffers() override;
		ASIOError controlPanel() override;
		ASIOError future(long selector, void* opt) override;
		ASIOError outputReady() override;

		// Observation for the probe: periods delivered, capture packets that
		// were not there in time (zero-filled), render periods the thread
		// could not hand to the device in time.
		struct Counters
		{
			uint64_t periods = 0;
			uint64_t inputUnderruns = 0;
			uint64_t outputMisses = 0;
			// Events that came more than 1.75 periods after the previous one:
			// a driver that signals at its own period rather than the one it
			// accepted, which leaves the gap unplayed at this buffer size.
			uint64_t slowEvents = 0;
			// The interval between consecutive events, averaged and at its
			// worst, in microseconds; what a driver's period really is.
			uint64_t eventIntervalAvgUs = 0;
			uint64_t eventIntervalMaxUs = 0;
			// How long servePeriod (the whole callback chain) took, at its worst.
			uint64_t serviceMaxUs = 0;
			// The device period in ASIO periods the stream settled on.
			uint64_t bridge = 1;
		};
		Counters counters() const noexcept;

	private:
		// One endpoint in one direction, from the device to the running stream.
		struct Port
		{
			bool capture = false;
			std::wstring endpointGuid;
			std::wstring friendlyName;
			IMMDevice* device = nullptr;
			IAudioClient* client = nullptr;
			IAudioRenderClient* render = nullptr;
			IAudioCaptureClient* captureClient = nullptr;
			HANDLE event = nullptr;
			std::vector<unsigned char> deviceFormat;   // WAVEFORMATEX blob, PKEY_AudioEngine_DeviceFormat
			unsigned channels = 0;
			unsigned long channelMask = 0;
			unsigned deviceRate = 0;
			REFERENCE_TIME minPeriodHns = 0;            // from GetDevicePeriod, independent of sample rate
			wasapi::Container container;                // negotiated for the current rate
			bool haveContainer = false;
			std::vector<std::vector<unsigned char>> planes[2];   // [half][channel], the ASIO buffers; live from createBuffers to disposeBuffers
			std::vector<void*> planePointers[2];         // [half][channel], stable aliases into planes
			std::vector<unsigned char> block;           // one interleaved device period (bridge ASIO periods)
			wasapi::CaptureQueue queue;                 // capture: packets not yet handed out
			std::atomic<long> latencyFrames{0};
			// The device period in ASIO periods. 1 on a driver that honours
			// the period it accepted; a driver that signals at its own coarser
			// cycle gets a device period of `bridge` ASIO periods and the host
			// is called that many times per event, gap-free (see streamThread).
			unsigned bridge = 1;
			wasapi::OutputStager stager;                // output: ASIO periods interleaved into block so far

			void closeStream() noexcept;
			void releasePlanes() noexcept;
			void closeDevice() noexcept;
		};

		bool openPort(Port& port, IMMDeviceEnumerator* enumerator, char* message);
		bool negotiate(Port& port, unsigned rate, wasapi::Container* found) const;
		HRESULT initializeStream(Port& port, long frames, unsigned bridge);
		bool prepareStreams(long frames, unsigned bridge, char* message);
		bool rebridge(unsigned factor) noexcept;
		void primeOutput() noexcept;
		bool startEndpoints() noexcept;
		void noteDeviceResult(HRESULT hr) noexcept;
		void streamThread() noexcept;
		void servePeriod(long half) noexcept;
		void commitOutput(long half) noexcept;
		void drainCapture(Port& port) noexcept;
		void setError(const char* message) noexcept;
		void fillTimeInfo(ASIOTime& time) const noexcept;

		std::atomic<long> refCount_{1};
		Port ports_[2];                   // [0] playback (outputs), [1] recording (inputs)
		bool initialized_ = false;
		bool prepared_ = false;
		std::atomic<bool> running_{false};
		std::atomic<bool> stopRequested_{false};
		std::atomic<bool> threadAlive_{false};
		std::atomic<unsigned long> threadId_{0};
		std::atomic<unsigned> bridge_{1};
		std::thread thread_;
		HANDLE stopEvent_ = nullptr;
		HANDLE startAckEvent_ = nullptr;
		std::atomic<long> startResult_{ASE_OK};
		ASIOCallbacks callbacks_ = {};
		bool hostSupportsTimeInfo_ = false;
		long frames_ = 0;
		unsigned rate_ = 0;
		std::atomic<long> pendingHalf_{-1};  // the half whose output has not been committed yet
		std::atomic<bool> committed_{false};
		HRESULT deviceResult_ = S_OK;        // stream thread: the worst device call result of the current event
		std::atomic<uint64_t> samplePosition_{0};
		Counters counters_;  // Stream thread writes; readers observe only after stop() joins.
		char errorMessage_[124] = {};
	};
}
