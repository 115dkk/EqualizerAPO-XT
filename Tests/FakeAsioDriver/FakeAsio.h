/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	A deterministic ASIO driver with no hardware behind it. The tests link
	this class directly; the FakeAsioDriver DLL wraps it in a class factory so
	the probe can load it the way a DAW loads a driver (DllGetClassObject, no
	registry). Its inputs come from a seeded generator, its outputs are
	recorded, and buffer switches happen only when pump() is called, so a run
	is reproducible to the byte.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "Tests/FakeAsioDriver/FakeAsioControl.h"

class FakeAsioDriver final : public IASIO, public IFakeAsioControl
{
public:
	FakeAsioDriver();
	~FakeAsioDriver();

	FakeAsioDriver(const FakeAsioDriver&) = delete;
	FakeAsioDriver& operator=(const FakeAsioDriver&) = delete;

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

	// IFakeAsioControl
	HRESULT STDMETHODCALLTYPE configure(const FakeAsioConfig* config) override;
	HRESULT STDMETHODCALLTYPE pump(long periods) override;
	HRESULT STDMETHODCALLTYPE capturedOutput(long channel, const unsigned char** data, unsigned long* bytes) override;
	HRESULT STDMETHODCALLTYPE suppliedInput(long channel, const unsigned char** data, unsigned long* bytes) override;
	HRESULT STDMETHODCALLTYPE clearRecords() override;
	HRESULT STDMETHODCALLTYPE raiseResetRequest() override;
	HRESULT STDMETHODCALLTYPE raiseSampleRateChange(double rate) override;
	HRESULT STDMETHODCALLTYPE counters(FakeAsioCounters* out) override;

	static long instanceCount() noexcept;

	// The generator the tests use to predict input contents: channel c,
	// sample n (counted from the first pump) as a float in [-0.5, 0.5).
	static float generatorSample(unsigned seed, long channel, uint64_t sampleIndex) noexcept;

private:
	struct Channel
	{
		bool input = false;
		long index = 0;
		bool open = false;
		std::vector<unsigned char> buffers[2];
	};

	// Records outlive the buffers: a test reads them after disposeBuffers().
	struct Record
	{
		bool input = false;
		long index = 0;
		std::vector<unsigned char> bytes;     // outputs: captured; inputs: supplied
	};

	Channel* findChannel(bool input, long index);
	Record& record(bool input, long index);
	void fillInputs(long half);
	void captureOutputs(long half);

	std::atomic<long> refCount_{1};
	FakeAsioConfig config_;
	bool initialized_ = false;
	bool buffersCreated_ = false;
	bool started_ = false;
	double sampleRate_ = 48000.0;
	long bufferSize_ = 0;
	unsigned bytesPerSample_ = 4;
	ASIOCallbacks callbacks_ = {};
	bool hostSupportsTimeInfo_ = false;
	std::vector<Channel> channels_;
	std::vector<Record> records_;
	uint64_t samplePosition_ = 0;
	uint64_t generatorPosition_ = 0;
	long pendingHalf_ = -1;
	bool capturedThisPeriod_ = false;
	FakeAsioCounters counters_;
};
