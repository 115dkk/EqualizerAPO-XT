/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Tests/FakeAsioDriver/FakeAsio.h"

#include <cstdio>
#include <cstring>

#include "asio/SampleCodec.h"

namespace
{
	std::atomic<long> instances{0};

	void splitInt64(uint64_t value, unsigned long& hi, unsigned long& lo) noexcept
	{
		hi = static_cast<unsigned long>(value >> 32);
		lo = static_cast<unsigned long>(value & 0xffffffffu);
	}
}

FakeAsioDriver::FakeAsioDriver()
{
	instances.fetch_add(1, std::memory_order_acq_rel);
}

FakeAsioDriver::~FakeAsioDriver()
{
	instances.fetch_sub(1, std::memory_order_acq_rel);
}

long FakeAsioDriver::instanceCount() noexcept
{
	return instances.load(std::memory_order_acquire);
}

float FakeAsioDriver::generatorSample(unsigned seed, long channel, uint64_t sampleIndex) noexcept
{
	// A per-channel LCG stepped to the sample index. Integer arithmetic only,
	// so every platform and bitness produces the same bytes.
	uint32_t state = (seed + 1u) * 2654435761u + static_cast<uint32_t>(channel + 1) * 40503u;
	state ^= static_cast<uint32_t>(sampleIndex * 2246822519u);
	state = state * 1664525u + 1013904223u;
	state ^= state >> 13;
	state = state * 1664525u + 1013904223u;
	return static_cast<float>(static_cast<int32_t>(state)) * (0.5f / 2147483648.0f);
}

// ---- IUnknown ----

HRESULT STDMETHODCALLTYPE FakeAsioDriver::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == CLSID_FakeAsio)
	{
		*object = static_cast<IASIO*>(this);
		AddRef();
		return S_OK;
	}
	if (riid == IID_IFakeAsioControl)
	{
		*object = static_cast<IFakeAsioControl*>(this);
		AddRef();
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE FakeAsioDriver::AddRef()
{
	return static_cast<ULONG>(refCount_.fetch_add(1, std::memory_order_acq_rel) + 1);
}

ULONG STDMETHODCALLTYPE FakeAsioDriver::Release()
{
	const long remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
	if (remaining == 0)
	{
		delete this;
		return 0;
	}
	return static_cast<ULONG>(remaining);
}

// ---- IASIO ----

ASIOBool FakeAsioDriver::init(void*)
{
	counters_.initCalls++;
	if (config_.failInit != 0)
		return ASIOFalse;
	initialized_ = true;
	sampleRate_ = config_.sampleRate;
	return ASIOTrue;
}

void FakeAsioDriver::getDriverName(char* name)
{
	std::memcpy(name, "FakeAsio", 9);
}

long FakeAsioDriver::getDriverVersion()
{
	return 1;
}

void FakeAsioDriver::getErrorMessage(char* string)
{
	std::memcpy(string, "FakeAsio: init was configured to fail", 38);
}

ASIOError FakeAsioDriver::start()
{
	if (!buffersCreated_)
		return ASE_NotPresent;
	counters_.startCalls++;
	started_ = true;
	return ASE_OK;
}

ASIOError FakeAsioDriver::stop()
{
	if (!started_)
		return ASE_NotPresent;
	counters_.stopCalls++;
	started_ = false;
	return ASE_OK;
}

ASIOError FakeAsioDriver::getChannels(long* numInputChannels, long* numOutputChannels)
{
	if (numInputChannels != nullptr)
		*numInputChannels = config_.inputChannels;
	if (numOutputChannels != nullptr)
		*numOutputChannels = config_.outputChannels;
	return ASE_OK;
}

ASIOError FakeAsioDriver::getLatencies(long* inputLatency, long* outputLatency)
{
	if (inputLatency != nullptr)
		*inputLatency = config_.inputLatency + bufferSize_;
	if (outputLatency != nullptr)
		*outputLatency = config_.outputLatency + bufferSize_;
	return ASE_OK;
}

ASIOError FakeAsioDriver::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
{
	if (minSize != nullptr)
		*minSize = config_.minSize;
	if (maxSize != nullptr)
		*maxSize = config_.maxSize;
	if (preferredSize != nullptr)
		*preferredSize = config_.preferredSize;
	if (granularity != nullptr)
		*granularity = config_.granularity;
	return ASE_OK;
}

ASIOError FakeAsioDriver::canSampleRate(ASIOSampleRate sampleRate)
{
	return sampleRate == 44100.0 || sampleRate == 48000.0 || sampleRate == 96000.0 || sampleRate == config_.sampleRate
		? ASE_OK : ASE_NoClock;
}

ASIOError FakeAsioDriver::getSampleRate(ASIOSampleRate* sampleRate)
{
	if (sampleRate == nullptr)
		return ASE_InvalidParameter;
	*sampleRate = sampleRate_;
	return ASE_OK;
}

ASIOError FakeAsioDriver::setSampleRate(ASIOSampleRate sampleRate)
{
	if (canSampleRate(sampleRate) != ASE_OK)
		return ASE_NoClock;
	if (sampleRate != sampleRate_)
	{
		sampleRate_ = sampleRate;
		if (buffersCreated_ && callbacks_.sampleRateDidChange != nullptr)
			callbacks_.sampleRateDidChange(sampleRate);
	}
	return ASE_OK;
}

ASIOError FakeAsioDriver::getClockSources(ASIOClockSource* clocks, long* numSources)
{
	if (clocks == nullptr || numSources == nullptr || *numSources < 1)
		return ASE_InvalidParameter;
	clocks[0].index = 0;
	clocks[0].associatedChannel = -1;
	clocks[0].associatedGroup = -1;
	clocks[0].isCurrentSource = ASIOTrue;
	std::memcpy(clocks[0].name, "Internal", 9);
	*numSources = 1;
	return ASE_OK;
}

ASIOError FakeAsioDriver::setClockSource(long reference)
{
	return reference == 0 ? ASE_OK : ASE_NotPresent;
}

ASIOError FakeAsioDriver::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
{
	if (sPos == nullptr || tStamp == nullptr)
		return ASE_InvalidParameter;
	const uint64_t nanos = sampleRate_ > 0.0
		? static_cast<uint64_t>(static_cast<double>(samplePosition_) * 1e9 / sampleRate_) : 0;
	splitInt64(samplePosition_, sPos->hi, sPos->lo);
	splitInt64(nanos, tStamp->hi, tStamp->lo);
	return ASE_OK;
}

ASIOError FakeAsioDriver::getChannelInfo(ASIOChannelInfo* info)
{
	if (info == nullptr)
		return ASE_InvalidParameter;
	const bool input = info->isInput != ASIOFalse;
	const long count = input ? config_.inputChannels : config_.outputChannels;
	if (info->channel < 0 || info->channel >= count)
		return ASE_InvalidParameter;
	Channel* channel = findChannel(input, info->channel);
	info->isActive = (channel != nullptr && channel->open) ? ASIOTrue : ASIOFalse;
	info->channelGroup = 0;
	info->type = config_.sampleType;
	std::snprintf(info->name, sizeof(info->name), "%s %ld", input ? "In" : "Out", info->channel + 1);
	return ASE_OK;
}

FakeAsioDriver::Channel* FakeAsioDriver::findChannel(bool input, long index)
{
	for (Channel& channel : channels_)
	{
		if (channel.input == input && channel.index == index)
			return &channel;
	}
	return nullptr;
}

FakeAsioDriver::Record& FakeAsioDriver::record(bool input, long index)
{
	for (size_t i = 0; i < records_.size(); i++)
	{
		if (records_[i].input == input && records_[i].index == index)
			return records_[i];
	}
	Record entry;
	entry.input = input;
	entry.index = index;
	records_.push_back(std::move(entry));
	return records_.back();
}

ASIOError FakeAsioDriver::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks)
{
	if (!initialized_)
		return ASE_NotPresent;
	if (buffersCreated_)
		return ASE_InvalidMode;
	if (bufferInfos == nullptr || numChannels <= 0 || callbacks == nullptr)
		return ASE_InvalidParameter;
	if (bufferSize < config_.minSize || bufferSize > config_.maxSize)
		return ASE_InvalidMode;

	eapo::asio::SampleCodec codec;
	if (!eapo::asio::findSampleCodec(config_.sampleType, codec))
	{
		// DSD: a real driver would still hand out buffers; the wrapper is
		// expected to refuse them before this point.
		bytesPerSample_ = 1;
	}
	else
	{
		bytesPerSample_ = codec.bytesPerSample;
	}

	counters_.createBuffersCalls++;
	channels_.clear();
	for (long i = 0; i < numChannels; i++)
	{
		ASIOBufferInfo& info = bufferInfos[i];
		const bool input = info.isInput != ASIOFalse;
		const long count = input ? config_.inputChannels : config_.outputChannels;
		if (info.channelNum < 0 || info.channelNum >= count || findChannel(input, info.channelNum) != nullptr)
		{
			channels_.clear();
			return ASE_InvalidParameter;
		}
		Channel channel;
		channel.input = input;
		channel.index = info.channelNum;
		channel.open = true;
		const size_t bytes = static_cast<size_t>(bytesPerSample_) * static_cast<size_t>(bufferSize);
		channel.buffers[0].assign(bytes, 0);
		channel.buffers[1].assign(bytes, 0);
		channels_.push_back(std::move(channel));
	}
	for (long i = 0; i < numChannels; i++)
	{
		Channel& channel = channels_[static_cast<size_t>(i)];
		bufferInfos[i].buffers[0] = channel.buffers[0].data();
		bufferInfos[i].buffers[1] = channel.buffers[1].data();
	}

	callbacks_ = *callbacks;
	bufferSize_ = bufferSize;
	hostSupportsTimeInfo_ = callbacks_.asioMessage != nullptr && callbacks_.bufferSwitchTimeInfo != nullptr
		&& callbacks_.asioMessage(kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
		&& callbacks_.asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
	counters_.hostSupportsTimeInfo = hostSupportsTimeInfo_ ? 1 : 0;
	buffersCreated_ = true;
	samplePosition_ = 0;
	pendingHalf_ = -1;
	return ASE_OK;
}

ASIOError FakeAsioDriver::disposeBuffers()
{
	if (!buffersCreated_)
		return ASE_InvalidMode;
	if (started_)
		stop();
	counters_.disposeBuffersCalls++;
	channels_.clear();
	buffersCreated_ = false;
	callbacks_ = ASIOCallbacks();
	return ASE_OK;
}

ASIOError FakeAsioDriver::controlPanel()
{
	return ASE_NotPresent;
}

ASIOError FakeAsioDriver::future(long selector, void*)
{
	return selector == kAsioCanTimeInfo ? ASE_SUCCESS : ASE_NotPresent;
}

ASIOError FakeAsioDriver::outputReady()
{
	counters_.outputReadyCalls++;
	if (pendingHalf_ >= 0 && !capturedThisPeriod_)
	{
		captureOutputs(pendingHalf_);
		capturedThisPeriod_ = true;
	}
	return ASE_OK;
}

// ---- IFakeAsioControl ----

HRESULT STDMETHODCALLTYPE FakeAsioDriver::configure(const FakeAsioConfig* config)
{
	if (config == nullptr)
		return E_POINTER;
	if (buffersCreated_)
		return E_UNEXPECTED;
	config_ = *config;
	sampleRate_ = config_.sampleRate;
	return S_OK;
}

void FakeAsioDriver::fillInputs(long half)
{
	eapo::asio::SampleCodec codec;
	if (!eapo::asio::findSampleCodec(config_.sampleType, codec))
		return;
	std::vector<float> samples(static_cast<size_t>(bufferSize_));
	for (Channel& channel : channels_)
	{
		if (!channel.input)
			continue;
		for (long n = 0; n < bufferSize_; n++)
			samples[static_cast<size_t>(n)] = generatorSample(config_.seed, channel.index, generatorPosition_ + static_cast<uint64_t>(n));
		codec.fromFloat(samples.data(), channel.buffers[half].data(), static_cast<unsigned>(bufferSize_));
		std::vector<unsigned char>& bytes = record(true, channel.index).bytes;
		bytes.insert(bytes.end(), channel.buffers[half].begin(), channel.buffers[half].end());
	}
	generatorPosition_ += static_cast<uint64_t>(bufferSize_);
}

void FakeAsioDriver::captureOutputs(long half)
{
	for (Channel& channel : channels_)
	{
		if (channel.input)
			continue;
		std::vector<unsigned char>& bytes = record(false, channel.index).bytes;
		bytes.insert(bytes.end(), channel.buffers[half].begin(), channel.buffers[half].end());
	}
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::pump(long periods)
{
	if (!started_)
		return E_UNEXPECTED;
	for (long p = 0; p < periods; p++)
	{
		const long half = static_cast<long>(counters_.periods & 1u);
		fillInputs(half);
		pendingHalf_ = half;
		capturedThisPeriod_ = false;
		if (hostSupportsTimeInfo_)
		{
			ASIOTime time = {};
			splitInt64(samplePosition_, time.timeInfo.samplePosition.hi, time.timeInfo.samplePosition.lo);
			const uint64_t nanos = static_cast<uint64_t>(static_cast<double>(samplePosition_) * 1e9 / sampleRate_);
			splitInt64(nanos, time.timeInfo.systemTime.hi, time.timeInfo.systemTime.lo);
			time.timeInfo.sampleRate = sampleRate_;
			time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
			callbacks_.bufferSwitchTimeInfo(&time, half, ASIOTrue);
			counters_.timeInfoSwitches++;
		}
		else if (callbacks_.bufferSwitch != nullptr)
		{
			callbacks_.bufferSwitch(half, ASIOTrue);
		}
		if (!capturedThisPeriod_)
		{
			captureOutputs(half);
			capturedThisPeriod_ = true;
		}
		pendingHalf_ = -1;
		samplePosition_ += static_cast<uint64_t>(bufferSize_);
		counters_.periods++;
		if (!started_)
			break;      // the host stopped us from inside the callback
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::capturedOutput(long index, const unsigned char** data, unsigned long* bytes)
{
	if (data == nullptr || bytes == nullptr)
		return E_POINTER;
	if (index < 0 || index >= config_.outputChannels)
		return E_INVALIDARG;
	const Record& entry = record(false, index);
	*data = entry.bytes.data();
	*bytes = static_cast<unsigned long>(entry.bytes.size());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::suppliedInput(long index, const unsigned char** data, unsigned long* bytes)
{
	if (data == nullptr || bytes == nullptr)
		return E_POINTER;
	if (index < 0 || index >= config_.inputChannels)
		return E_INVALIDARG;
	const Record& entry = record(true, index);
	*data = entry.bytes.data();
	*bytes = static_cast<unsigned long>(entry.bytes.size());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::clearRecords()
{
	records_.clear();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::raiseResetRequest()
{
	if (!buffersCreated_ || callbacks_.asioMessage == nullptr)
		return E_UNEXPECTED;
	counters_.lastResetRequestAnswer = callbacks_.asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::raiseSampleRateChange(double rate)
{
	sampleRate_ = rate;
	if (buffersCreated_ && callbacks_.sampleRateDidChange != nullptr)
		callbacks_.sampleRateDidChange(rate);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FakeAsioDriver::counters(FakeAsioCounters* out)
{
	if (out == nullptr)
		return E_POINTER;
	*out = counters_;
	return S_OK;
}
