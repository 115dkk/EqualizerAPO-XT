/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/AsioWrapper.h"

#include <cstring>
#include <new>

namespace eapo::asio
{
	namespace
	{
		std::atomic<long> instances{0};

		constexpr unsigned outputSlot = static_cast<unsigned>(Direction::Output);
		constexpr unsigned inputSlot = static_cast<unsigned>(Direction::Input);

		inline unsigned slotOf(Direction direction) noexcept
		{
			return static_cast<unsigned>(direction);
		}

		// Driver names are ASCII by convention; widen byte-wise so no code page
		// decision is made inside a DAW's process.
		void widen(const char* source, wchar_t* destination, size_t capacity) noexcept
		{
			size_t i = 0;
			for (; i + 1 < capacity && source[i] != '\0'; i++)
				destination[i] = static_cast<wchar_t>(static_cast<unsigned char>(source[i]));
			destination[i] = L'\0';
		}

		inline uint64_t tickNow() noexcept
		{
			LARGE_INTEGER counter;
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		const char* const driverSuffix = " (EQ APO XT)";
	}

	AsioWrapper::AsioWrapper(IASIO* target, const GUID& wrapperClsid, const std::wstring& targetClsid,
		StreamOptions options, std::unique_ptr<IStreamProcessor> processor)
		: target_(target), wrapperClsid_(wrapperClsid), targetClsid_(targetClsid),
		options_(std::move(options)), processor_(std::move(processor))
	{
		target_->AddRef();
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		tickToMicros_ = frequency.QuadPart > 0 ? 1000000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
		instances.fetch_add(1, std::memory_order_acq_rel);
	}

	AsioWrapper::~AsioWrapper()
	{
		// A host that releases the driver without disposing its buffers still
		// gets a clean teardown: stop the target and close the processor.
		if (state() == State::Running)
			stop();
		if (state() == State::Prepared)
			disposeBuffers();
		target_->Release();
		instances.fetch_sub(1, std::memory_order_acq_rel);
	}

	long AsioWrapper::instanceCount() noexcept
	{
		return instances.load(std::memory_order_acquire);
	}

	// ---- IUnknown ----

	HRESULT STDMETHODCALLTYPE AsioWrapper::QueryInterface(REFIID riid, void** object)
	{
		if (object == nullptr)
			return E_POINTER;
		// ASIO hosts ask for the driver's own CLSID as the interface id.
		if (riid == IID_IUnknown || riid == wrapperClsid_)
		{
			*object = static_cast<IASIO*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AsioWrapper::AddRef()
	{
		return static_cast<ULONG>(refCount_.fetch_add(1, std::memory_order_acq_rel) + 1);
	}

	ULONG STDMETHODCALLTYPE AsioWrapper::Release()
	{
		const long remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (remaining == 0)
		{
			delete this;
			return 0;
		}
		return static_cast<ULONG>(remaining);
	}

	// ---- control surface ----

	void AsioWrapper::setError(const char* message) noexcept
	{
		size_t i = 0;
		for (; i + 1 < sizeof(errorMessage_) && message[i] != '\0'; i++)
			errorMessage_[i] = message[i];
		errorMessage_[i] = '\0';
	}

	ASIOBool AsioWrapper::init(void* sysHandle)
	{
		// Hosts init every listed driver to enumerate it, sometimes twice; a
		// second init on a live instance is a yes, not a restart. The
		// processor is never touched here (decision 6).
		if (state() != State::Loaded)
			return ASIOTrue;
		if (target_->init(sysHandle) == ASIOFalse)
		{
			char message[124] = {};
			target_->getErrorMessage(message);
			setError(message);
			return ASIOFalse;
		}
		errorMessage_[0] = '\0';
		state_.store(State::Initialized, std::memory_order_release);
		return ASIOTrue;
	}

	void AsioWrapper::getDriverName(char* name)
	{
		char targetName[32] = {};
		target_->getDriverName(targetName);
		targetName[31] = '\0';
		const size_t suffixLength = std::strlen(driverSuffix);
		size_t baseLength = std::strlen(targetName);
		if (baseLength + suffixLength > 31)
			baseLength = 31 - suffixLength;
		std::memcpy(name, targetName, baseLength);
		std::memcpy(name + baseLength, driverSuffix, suffixLength);
		name[baseLength + suffixLength] = '\0';
	}

	long AsioWrapper::getDriverVersion()
	{
		return target_->getDriverVersion();
	}

	void AsioWrapper::getErrorMessage(char* string)
	{
		if (errorMessage_[0] != '\0')
		{
			std::memcpy(string, errorMessage_, sizeof(errorMessage_));
			return;
		}
		target_->getErrorMessage(string);
	}

	ASIOError AsioWrapper::getChannels(long* numInputChannels, long* numOutputChannels)
	{
		return target_->getChannels(numInputChannels, numOutputChannels);
	}

	ASIOError AsioWrapper::getLatencies(long* inputLatency, long* outputLatency)
	{
		const ASIOError error = target_->getLatencies(inputLatency, outputLatency);
		if (error != ASE_OK || state() < State::Prepared)
			return error;
		if (inputLatency != nullptr && processorEnabled(Direction::Input))
			*inputLatency += static_cast<long>(extraLatencyFrames_);
		if (outputLatency != nullptr && processorEnabled(Direction::Output))
			*outputLatency += static_cast<long>(extraLatencyFrames_);
		return error;
	}

	ASIOError AsioWrapper::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		return target_->getBufferSize(minSize, maxSize, preferredSize, granularity);
	}

	ASIOError AsioWrapper::canSampleRate(ASIOSampleRate sampleRate)
	{
		return target_->canSampleRate(sampleRate);
	}

	ASIOError AsioWrapper::getSampleRate(ASIOSampleRate* sampleRate)
	{
		return target_->getSampleRate(sampleRate);
	}

	ASIOError AsioWrapper::setSampleRate(ASIOSampleRate sampleRate)
	{
		// The target answers with sampleRateDidChange, which is where a live
		// stream is marked stale and the DAW is asked to reset.
		return target_->setSampleRate(sampleRate);
	}

	ASIOError AsioWrapper::getClockSources(ASIOClockSource* clocks, long* numSources)
	{
		return target_->getClockSources(clocks, numSources);
	}

	ASIOError AsioWrapper::setClockSource(long reference)
	{
		return target_->setClockSource(reference);
	}

	ASIOError AsioWrapper::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
	{
		return target_->getSamplePosition(sPos, tStamp);
	}

	ASIOError AsioWrapper::getChannelInfo(ASIOChannelInfo* info)
	{
		return target_->getChannelInfo(info);
	}

	ASIOError AsioWrapper::controlPanel()
	{
		return target_->controlPanel();
	}

	ASIOError AsioWrapper::future(long selector, void* opt)
	{
		return target_->future(selector, opt);
	}

	// ---- buffers ----

	ASIOError AsioWrapper::prepareChannels(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize)
	{
		long inputs = 0, outputs = 0;
		ASIOError error = target_->getChannels(&inputs, &outputs);
		if (error != ASE_OK)
			return error;
		if (inputs < 0 || outputs < 0 || inputs + outputs == 0)
		{
			setError("The wrapped driver reports no channels");
			return ASE_NotPresent;
		}

		inputCount_ = inputs;
		outputCount_ = outputs;
		try
		{
			channels_.assign(static_cast<size_t>(inputs + outputs), Channel());
			targetInfos_.assign(channels_.size(), ASIOBufferInfo());
		}
		catch (const std::bad_alloc&)
		{
			releaseChannels();
			return ASE_NoMemory;
		}

		for (size_t i = 0; i < channels_.size(); i++)
		{
			Channel& channel = channels_[i];
			channel.input = i < static_cast<size_t>(inputs);
			channel.index = channel.input ? static_cast<long>(i) : static_cast<long>(i) - inputs;

			ASIOChannelInfo info = {};
			info.channel = channel.index;
			info.isInput = channel.input ? ASIOTrue : ASIOFalse;
			error = target_->getChannelInfo(&info);
			if (error != ASE_OK)
			{
				releaseChannels();
				return error;
			}
			if (!findSampleCodec(info.type, channel.codec))
			{
				releaseChannels();
				setError("The wrapped driver uses a sample format EQ APO XT cannot process (DSD?)");
				return ASE_InvalidMode;
			}

			targetInfos_[i].isInput = info.isInput;
			targetInfos_[i].channelNum = channel.index;
			targetInfos_[i].buffers[0] = nullptr;
			targetInfos_[i].buffers[1] = nullptr;
		}

		for (long i = 0; i < numChannels; i++)
		{
			ASIOBufferInfo& info = bufferInfos[i];
			const bool input = info.isInput != ASIOFalse;
			const long count = input ? inputs : outputs;
			if (info.channelNum < 0 || info.channelNum >= count)
			{
				releaseChannels();
				return ASE_InvalidParameter;
			}
			Channel& channel = channels_[static_cast<size_t>(input ? info.channelNum : inputs + info.channelNum)];
			if (channel.hostInfo != nullptr)
			{
				releaseChannels();
				return ASE_InvalidParameter;
			}
			channel.hostInfo = &info;
			const size_t bytes = static_cast<size_t>(channel.codec.bytesPerSample) * static_cast<size_t>(bufferSize);
			for (int half = 0; half < 2; half++)
			{
				channel.hostBuffers[half] = new(std::nothrow) unsigned char[bytes];
				if (channel.hostBuffers[half] == nullptr)
				{
					releaseChannels();
					return ASE_NoMemory;
				}
				std::memset(channel.hostBuffers[half], 0, bytes);
			}
		}
		return ASE_OK;
	}

	void AsioWrapper::releaseChannels() noexcept
	{
		for (Channel& channel : channels_)
		{
			for (int half = 0; half < 2; half++)
			{
				delete[] static_cast<unsigned char*>(channel.hostBuffers[half]);
				channel.hostBuffers[half] = nullptr;
			}
			if (channel.hostInfo != nullptr)
			{
				channel.hostInfo->buffers[0] = nullptr;
				channel.hostInfo->buffers[1] = nullptr;
			}
		}
		channels_.clear();
		targetInfos_.clear();
		inputCount_ = 0;
		outputCount_ = 0;
	}

	bool AsioWrapper::fillFormat(long bufferSize)
	{
		format_ = StreamFormat();
		ASIOSampleRate rate = 0.0;
		if (target_->getSampleRate(&rate) != ASE_OK || !(rate > 0.0))
		{
			setError("The wrapped driver did not report a sample rate");
			return false;
		}
		format_.sampleRate = rate;
		format_.frames = static_cast<uint32_t>(bufferSize);
		format_.channels[outputSlot] = options_.processOutput ? static_cast<uint32_t>(outputCount_) : 0;
		format_.channels[inputSlot] = options_.processInput ? static_cast<uint32_t>(inputCount_) : 0;
		format_.mode = options_.mode;
		format_.deadlineUs = syncDeadlineUs(format_, options_);

		char targetName[32] = {};
		target_->getDriverName(targetName);
		targetName[31] = '\0';
		widen(targetName, format_.deviceName, sizeof(format_.deviceName) / sizeof(wchar_t));

		const size_t guidCapacity = sizeof(format_.deviceGuid) / sizeof(wchar_t);
		size_t i = 0;
		for (; i + 1 < guidCapacity && i < targetClsid_.size(); i++)
			format_.deviceGuid[i] = targetClsid_[i];
		format_.deviceGuid[i] = L'\0';
		return true;
	}

	ASIOError AsioWrapper::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks)
	{
		if (state() != State::Initialized)
			return ASE_InvalidMode;
		if (bufferInfos == nullptr || numChannels <= 0 || bufferSize <= 0 || callbacks == nullptr)
			return ASE_InvalidParameter;

		host_ = *callbacks;
		hostPresent_ = true;
		hostSupportsTimeInfo_ = host_.bufferSwitchTimeInfo != nullptr && host_.asioMessage != nullptr
			&& host_.asioMessage(kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1
			&& host_.asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
		hostSupportsResetRequest_ = host_.asioMessage != nullptr
			&& host_.asioMessage(kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) == 1;

		ASIOError error = prepareChannels(bufferInfos, numChannels, bufferSize);
		if (error != ASE_OK)
			return error;

		trampolines_ = CallbackTrampolines::claim(this);
		if (trampolines_ == nullptr)
		{
			releaseChannels();
			setError("EQ APO XT: no free callback slot in this process");
			return ASE_NoMemory;
		}

		error = target_->createBuffers(targetInfos_.data(), static_cast<long>(targetInfos_.size()), bufferSize, trampolines_);
		if (error != ASE_OK)
		{
			CallbackTrampolines::release(this);
			trampolines_ = nullptr;
			releaseChannels();
			return error;
		}

		for (size_t i = 0; i < channels_.size(); i++)
		{
			channels_[i].targetBuffers[0] = targetInfos_[i].buffers[0];
			channels_[i].targetBuffers[1] = targetInfos_[i].buffers[1];
			if (channels_[i].hostInfo != nullptr)
			{
				channels_[i].hostInfo->buffers[0] = channels_[i].hostBuffers[0];
				channels_[i].hostInfo->buffers[1] = channels_[i].hostBuffers[1];
			}
		}

		auto unwind = [this]() noexcept {
			target_->disposeBuffers();
			CallbackTrampolines::release(this);
			for (int i = 0; i < 2000 && !CallbackTrampolines::drained(trampolines_); i++)
				Sleep(1);
			trampolines_ = nullptr;
			releaseChannels();
		};

		if (!fillFormat(bufferSize))
		{
			unwind();
			return ASE_HWMalfunction;
		}

		OpenReport report;
		try
		{
			report = processor_->open(format_, options_);
		}
		catch (...)
		{
			report.status = OpenReport::Status::Unavailable;
			std::memcpy(report.message, "EQ APO XT engine host failed while opening the stream", 54);
		}
		if (report.status != OpenReport::Status::Ok)
		{
			unwind();
			setError(report.message[0] != '\0' ? report.message : "EQ APO XT engine host is not available");
			return report.status == OpenReport::Status::Rejected ? ASE_InvalidMode : ASE_HWMalfunction;
		}

		planes_[outputSlot] = format_.channels[outputSlot] > 0 ? report.planes[outputSlot] : nullptr;
		planes_[inputSlot] = format_.channels[inputSlot] > 0 ? report.planes[inputSlot] : nullptr;
		extraLatencyFrames_ = report.extraLatencyFrames;
		stats_ = StreamStats();
		frames_ = bufferSize;
		formatStale_.store(false, std::memory_order_release);
		processorGone_.store(false, std::memory_order_release);
		hostUsesOutputReady_.store(false, std::memory_order_release);
		pendingOutputIndex_.store(-1, std::memory_order_release);
		errorMessage_[0] = '\0';
		state_.store(State::Prepared, std::memory_order_release);
		return ASE_OK;
	}

	ASIOError AsioWrapper::disposeBuffers()
	{
		if (state() == State::Running)
			stop();
		if (state() != State::Prepared)
			return ASE_InvalidMode;

		// Refuse new switches first, then take the target down, then wait for
		// a switch that was already inside the wrapper to leave.
		state_.store(State::Initialized, std::memory_order_release);
		const ASIOError error = target_->disposeBuffers();
		CallbackTrampolines::release(this);
		for (int i = 0; i < 2000 && !CallbackTrampolines::drained(trampolines_); i++)
			Sleep(1);
		// A timed-out slot remains closed with entrants and is quarantined: claim
		// cannot reuse it until the stalled callbacks leave.
		trampolines_ = nullptr;

		processor_->close(stats_);
		planes_[outputSlot] = nullptr;
		planes_[inputSlot] = nullptr;
		releaseChannels();
		hostPresent_ = false;
		return error;
	}

	ASIOError AsioWrapper::start()
	{
		if (state() != State::Prepared)
			return ASE_InvalidMode;
		if (processorGone_.load(std::memory_order_acquire))
		{
			setError("EQ APO XT engine host went away; reopen the device");
			return ASE_HWMalfunction;
		}
		pendingOutputIndex_.store(-1, std::memory_order_release);
		state_.store(State::Running, std::memory_order_release);
		const ASIOError error = target_->start();
		if (error != ASE_OK)
			state_.store(State::Prepared, std::memory_order_release);
		return error;
	}

	ASIOError AsioWrapper::stop()
	{
		if (state() != State::Running)
			return ASE_InvalidMode;
		const ASIOError error = target_->stop();
		state_.store(State::Prepared, std::memory_order_release);
		return error;
	}

	ASIOError AsioWrapper::outputReady()
	{
		hostUsesOutputReady_.store(true, std::memory_order_release);
		const long index = pendingOutputIndex_.exchange(-1, std::memory_order_acq_rel);
		if (index >= 0 && state() == State::Running)
			commitOutput(index);
		return target_->outputReady();
	}

	// ---- the buffer switch ----

	bool AsioWrapper::processorEnabled(Direction direction) const noexcept
	{
		return planes_[slotOf(direction)] != nullptr && format_.channelCount(direction) > 0;
	}

	void AsioWrapper::onBufferSwitch(long doubleBufferIndex, ASIOBool directProcess) noexcept
	{
		switchBuffers(doubleBufferIndex, directProcess, nullptr);
	}

	ASIOTime* AsioWrapper::onBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) noexcept
	{
		switchBuffers(doubleBufferIndex, directProcess, params);
		return params;
	}

	void AsioWrapper::switchBuffers(long index, ASIOBool direct, ASIOTime* time) noexcept
	{
		if (state() != State::Running || index < 0 || index > 1)
			return;

		runInput(index);

		pendingOutputIndex_.store(index, std::memory_order_release);
		if (hostSupportsTimeInfo_)
		{
			ASIOTime local = {};
			ASIOTime* forwarded = time;
			if (forwarded == nullptr)
			{
				// The target used the old callback but the host wants time
				// info: hand it what the target can tell us.
				if (target_->getSamplePosition(&local.timeInfo.samplePosition, &local.timeInfo.systemTime) == ASE_OK)
					local.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
				local.timeInfo.sampleRate = format_.sampleRate;
				local.timeInfo.flags |= kSampleRateValid;
				forwarded = &local;
			}
			host_.bufferSwitchTimeInfo(forwarded, index, direct);
		}
		else if (host_.bufferSwitch != nullptr)
		{
			host_.bufferSwitch(index, direct);
		}

		if (!hostUsesOutputReady_.load(std::memory_order_acquire))
		{
			const long pending = pendingOutputIndex_.exchange(-1, std::memory_order_acq_rel);
			if (pending == index)
				commitOutput(index);
		}
	}

	void AsioWrapper::account(Direction direction, Outcome outcome, uint64_t startTick) noexcept
	{
		const unsigned slot = slotOf(direction);
		const double micros = static_cast<double>(tickNow() - startTick) * tickToMicros_;
		const uint32_t elapsed = micros > 4294967295.0 ? 4294967295u : static_cast<uint32_t>(micros);
		stats_.blocks[slot]++;
		stats_.lastProcessUs[slot] = elapsed;
		if (elapsed > stats_.maxProcessUs[slot])
			stats_.maxProcessUs[slot] = elapsed;
		if (outcome == Outcome::Late)
			stats_.late[slot]++;
		else if (outcome == Outcome::Gone)
		{
			stats_.gone[slot]++;
			processorGone_.store(true, std::memory_order_release);
		}
	}

	void AsioWrapper::runInput(long index) noexcept
	{
		if (inputCount_ == 0)
			return;
		const size_t first = 0;
		const size_t end = static_cast<size_t>(inputCount_);
		bool processed = false;

		if (processorEnabled(Direction::Input) && !processorGone_.load(std::memory_order_acquire))
		{
			if (formatStale_.load(std::memory_order_acquire))
			{
				stats_.staleBlocks++;
			}
			else
			{
				float** planes = planes_[inputSlot];
				for (size_t i = first; i < end; i++)
					channels_[i].codec.toFloat(channels_[i].targetBuffers[index], planes[i], static_cast<unsigned>(frames_));
				const uint64_t started = tickNow();
				const Outcome outcome = processor_->process(Direction::Input);
				account(Direction::Input, outcome, started);
				if (outcome == Outcome::Processed)
				{
					for (size_t i = first; i < end; i++)
					{
						if (channels_[i].hostBuffers[index] != nullptr)
							channels_[i].codec.fromFloat(planes[i], channels_[i].hostBuffers[index], static_cast<unsigned>(frames_));
					}
					processed = true;
				}
			}
		}

		if (!processed)
		{
			for (size_t i = first; i < end; i++)
			{
				if (channels_[i].hostBuffers[index] != nullptr)
				{
					std::memcpy(channels_[i].hostBuffers[index], channels_[i].targetBuffers[index],
						static_cast<size_t>(channels_[i].codec.bytesPerSample) * static_cast<size_t>(frames_));
				}
			}
		}
	}

	void AsioWrapper::commitOutput(long index) noexcept
	{
		if (outputCount_ == 0)
			return;
		const size_t first = static_cast<size_t>(inputCount_);
		const size_t end = channels_.size();
		bool processed = false;

		if (processorEnabled(Direction::Output) && !processorGone_.load(std::memory_order_acquire))
		{
			if (formatStale_.load(std::memory_order_acquire))
			{
				stats_.staleBlocks++;
			}
			else
			{
				float** planes = planes_[outputSlot];
				for (size_t i = first; i < end; i++)
				{
					float* plane = planes[i - first];
					if (channels_[i].hostBuffers[index] != nullptr)
						channels_[i].codec.toFloat(channels_[i].hostBuffers[index], plane, static_cast<unsigned>(frames_));
					else
						std::memset(plane, 0, sizeof(float) * static_cast<size_t>(frames_));
				}
				const uint64_t started = tickNow();
				const Outcome outcome = processor_->process(Direction::Output);
				account(Direction::Output, outcome, started);
				if (outcome == Outcome::Processed)
				{
					for (size_t i = first; i < end; i++)
						channels_[i].codec.fromFloat(planes[i - first], channels_[i].targetBuffers[index], static_cast<unsigned>(frames_));
					processed = true;
				}
			}
		}

		if (!processed)
		{
			for (size_t i = first; i < end; i++)
			{
				const size_t bytes = static_cast<size_t>(channels_[i].codec.bytesPerSample) * static_cast<size_t>(frames_);
				if (channels_[i].hostBuffers[index] != nullptr)
					std::memcpy(channels_[i].targetBuffers[index], channels_[i].hostBuffers[index], bytes);
				else
					std::memset(channels_[i].targetBuffers[index], 0, bytes);
			}
		}
	}

	// ---- target notifications ----

	void AsioWrapper::onSampleRateDidChange(ASIOSampleRate rate) noexcept
	{
		bool askForReset = false;
		if (state() >= State::Prepared && rate != format_.sampleRate)
		{
			// The engines were built for the old rate. Filtering at the wrong
			// rate is worse than no filtering, so pass through until the DAW
			// reopens the device, and ask it to.
			formatStale_.store(true, std::memory_order_release);
			askForReset = true;
		}
		if (hostPresent_ && host_.sampleRateDidChange != nullptr)
			host_.sampleRateDidChange(rate);
		if (askForReset && hostPresent_ && hostSupportsResetRequest_)
			host_.asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
	}

	long AsioWrapper::onAsioMessage(long selector, long value, void* message, double* opt) noexcept
	{
		switch (selector)
		{
		case kAsioSelectorSupported:
			if (value == kAsioSupportsTimeInfo)
				return hostSupportsTimeInfo_ ? 1 : 0;
			break;
		case kAsioSupportsTimeInfo:
			// Answered from what the DAW told us at createBuffers, which is
			// why the DAW is asked before the target's createBuffers runs.
			return hostSupportsTimeInfo_ ? 1 : 0;
		case kAsioEngineVersion:
			if (!hostPresent_ || host_.asioMessage == nullptr)
				return 2;
			break;
		default:
			break;
		}
		if (hostPresent_ && host_.asioMessage != nullptr)
			return host_.asioMessage(selector, value, message, opt);
		return 0;
	}
}
