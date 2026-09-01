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

	The pure parts (container order, buffer-size policy, interleaving) live
	in the wasapi namespace so the tests can pin them without a device.
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
			std::vector<unsigned char> pending;         // capture: packets not yet handed out
			size_t pendingFrames = 0;
			std::atomic<long> latencyFrames{0};
			// The device period in ASIO periods. 1 on a driver that honours
			// the period it accepted; a driver that signals at its own coarser
			// cycle gets a device period of `bridge` ASIO periods and the host
			// is called that many times per event, gap-free (see streamThread).
			unsigned bridge = 1;
			unsigned staged = 0;                        // output: ASIO periods interleaved into block so far

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
		std::atomic<uint64_t> samplePosition_{0};
		Counters counters_;  // Stream thread writes; readers observe only after stop() joins.
		char errorMessage_[124] = {};
	};
}
