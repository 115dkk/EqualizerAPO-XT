/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The ASIO wrapper: the driver a DAW loads. Toward the DAW it is an IASIO;
	behind it sits the real hardware driver (the target) and, at the processor
	seam, whatever transforms the audio (docs/architecture/asio-host-study.md,
	section 10). The wrapper owns the DAW-facing buffers, opens every physical
	channel of the target so the engine sees the whole device, converts
	between the target's sample types and the processor's float32 planes at
	its edge, and forwards every IASIO call it has no opinion about.

	State machine (DAW control thread):

	  Loaded -init-> Initialized -createBuffers-> Prepared -start-> Running
	  Running -stop-> Prepared -disposeBuffers-> Initialized

	A call out of order answers ASE_InvalidMode and never reaches the target.
	createBuffers is the readiness barrier: the target's buffers are created
	first (so rate, channel counts and buffer size are final), then the
	processor is opened, and a processor that does not become ready makes
	createBuffers fail loudly with ASE_HWMalfunction. There is no silent
	unprocessed start.

	Per buffer switch (the target's thread): target input -> planes ->
	process(Input) -> DAW input; DAW bufferSwitch; DAW output -> planes ->
	process(Output) -> target output. A host that calls outputReady() gets its
	output committed there; one that does not gets it committed when its
	bufferSwitch returns.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "asio/AsioSdk.h"
#include "asio/CallbackTrampolines.h"
#include "asio/SampleCodec.h"
#include "asio/StreamProcessor.h"

namespace eapo::asio
{
	class AsioWrapper final : public IASIO, public ITargetCallbackSink
	{
	public:
		enum class State : uint32_t
		{
			Loaded,
			Initialized,
			Prepared,
			Running
		};

		// `target` is AddRef'd for the wrapper's lifetime. `wrapperClsid` is the
		// IID the DAW will QueryInterface with (ASIO's IID == CLSID quirk);
		// `targetClsid` is what the engine's Device: line sees as the GUID.
		AsioWrapper(IASIO* target, const GUID& wrapperClsid, const std::wstring& targetClsid,
			StreamOptions options, std::unique_ptr<IStreamProcessor> processor);
		~AsioWrapper() override;

		AsioWrapper(const AsioWrapper&) = delete;
		AsioWrapper& operator=(const AsioWrapper&) = delete;

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

		// Observation for the probe and the tests. stats() is only stable
		// while no buffer switch is running (Prepared/Initialized).
		State state() const noexcept {return state_.load(std::memory_order_acquire);}
		const StreamStats& stats() const noexcept {return stats_;}
		const StreamFormat& format() const noexcept {return format_;}
		const IStreamProcessor* processor() const noexcept {return processor_.get();}

		static long instanceCount() noexcept;

	private:
		struct Channel
		{
			bool input = false;
			long index = 0;                   // channel number on the target
			SampleCodec codec;
			void* targetBuffers[2] = {nullptr, nullptr};
			void* hostBuffers[2] = {nullptr, nullptr};   // owned; nullptr when the DAW did not open the channel
			ASIOBufferInfo* hostInfo = nullptr;          // the DAW's entry, for the pointer hand-back
		};

		// ITargetCallbackSink
		void onBufferSwitch(long doubleBufferIndex, ASIOBool directProcess) noexcept override;
		ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) noexcept override;
		void onSampleRateDidChange(ASIOSampleRate rate) noexcept override;
		long onAsioMessage(long selector, long value, void* message, double* opt) noexcept override;

		ASIOError prepareChannels(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize);
		void releaseChannels() noexcept;
		bool fillFormat(long bufferSize);
		void setError(const char* message) noexcept;

		void switchBuffers(long index, ASIOBool direct, ASIOTime* time) noexcept;
		void runInput(long index) noexcept;
		void commitOutput(long index) noexcept;
		void account(Direction direction, Outcome outcome, uint64_t startTick) noexcept;
		bool processorEnabled(Direction direction) const noexcept;

		std::atomic<long> refCount_{1};
		IASIO* target_;
		GUID wrapperClsid_;
		std::wstring targetClsid_;
		StreamOptions options_;
		std::unique_ptr<IStreamProcessor> processor_;

		std::atomic<State> state_{State::Loaded};
		char errorMessage_[124] = {};

		ASIOCallbacks host_ = {};
		bool hostPresent_ = false;
		bool hostSupportsTimeInfo_ = false;
		bool hostSupportsResetRequest_ = false;
		std::atomic<bool> hostUsesOutputReady_{false};
		std::atomic<long> pendingOutputIndex_{-1};
		std::atomic<bool> formatStale_{false};
		std::atomic<bool> processorGone_{false};

		std::vector<Channel> channels_;   // every physical channel, inputs first
		std::vector<ASIOBufferInfo> targetInfos_;
		long inputCount_ = 0;
		long outputCount_ = 0;
		long frames_ = 0;
		ASIOCallbacks* trampolines_ = nullptr;

		StreamFormat format_;
		float** planes_[directionCount] = {nullptr, nullptr};
		uint32_t extraLatencyFrames_ = 0;
		StreamStats stats_;
		double tickToMicros_ = 0.0;
	};
}
