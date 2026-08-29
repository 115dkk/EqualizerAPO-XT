/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The DAW stand-in shared by AsioTests and AsioProbe. It opens channels on a
	driver, answers the driver's asioMessage queries, writes a deterministic
	signal into its output buffers on every buffer switch, records what
	arrives in its input buffers, and remembers the notifications it
	received. ASIO callbacks carry no context, so one stub is live at a time.
*/

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <avrt.h>

#include "asio/AsioSdk.h"
#include "asio/SampleCodec.h"

namespace asiotest
{
	class HostStub
	{
	public:
		struct Options
		{
			bool supportsTimeInfo = true;
			bool supportsResetRequest = true;
			bool callOutputReady = false;
			bool proAudioCallback = false;   // MMCSS Pro Audio on the driver's callback thread, as a DAW does
			unsigned outputSeed = 7;      // 0 = silence
			float outputScale = 1.0f;
			long sampleType = ASIOSTInt32LSB;
			double sineHz = 0.0;          // > 0: a sine at outputScale on every output instead of the noise
			double sampleRate = 48000.0;  // the sine's clock
		};

		explicit HostStub(Options options)
			: options_(options)
		{
			callbacks_.bufferSwitch = &HostStub::bufferSwitchThunk;
			callbacks_.sampleRateDidChange = &HostStub::sampleRateDidChangeThunk;
			callbacks_.asioMessage = &HostStub::asioMessageThunk;
			callbacks_.bufferSwitchTimeInfo = &HostStub::bufferSwitchTimeInfoThunk;
			eapo::asio::findSampleCodec(options.sampleType, codec_);
			current_ = this;
		}

		~HostStub()
		{
			if (current_ == this)
				current_ = nullptr;
		}

		HostStub(const HostStub&) = delete;
		HostStub& operator=(const HostStub&) = delete;

		// Chooses the channels to open; createBuffers() then hands the
		// resulting infos to the driver.
		void openChannels(long inputs, long outputs)
		{
			infos_.clear();
			for (long i = 0; i < inputs; i++)
			{
				ASIOBufferInfo info = {};
				info.isInput = ASIOTrue;
				info.channelNum = i;
				infos_.push_back(info);
			}
			for (long i = 0; i < outputs; i++)
			{
				ASIOBufferInfo info = {};
				info.isInput = ASIOFalse;
				info.channelNum = i;
				infos_.push_back(info);
			}
		}

		void openChannelList(const std::vector<long>& inputs, const std::vector<long>& outputs)
		{
			infos_.clear();
			for (long i : inputs)
			{
				ASIOBufferInfo info = {};
				info.isInput = ASIOTrue;
				info.channelNum = i;
				infos_.push_back(info);
			}
			for (long i : outputs)
			{
				ASIOBufferInfo info = {};
				info.isInput = ASIOFalse;
				info.channelNum = i;
				infos_.push_back(info);
			}
		}

		ASIOError createBuffers(IASIO* driver, long frames)
		{
			driver_ = driver;
			frames_ = frames;
			outputPosition_ = 0;
			inputRecords_.assign(infos_.size(), std::vector<unsigned char>());
			return driver->createBuffers(infos_.data(), static_cast<long>(infos_.size()), frames, &callbacks_);
		}

		ASIOError disposeBuffers()
		{
			return driver_ != nullptr ? driver_->disposeBuffers() : ASE_InvalidMode;
		}

		// What the host would have written: channel c, sample n from the
		// first switch, before the codec.
		float outputSample(long channel, uint64_t sampleIndex) const noexcept
		{
			if (options_.sineHz > 0.0)
				return static_cast<float>(std::sin(6.283185307179586 * options_.sineHz * static_cast<double>(sampleIndex) / options_.sampleRate)) * options_.outputScale;
			if (options_.outputSeed == 0)
				return 0.0f;
			uint32_t state = (options_.outputSeed + 3u) * 2246822519u + static_cast<uint32_t>(channel + 1) * 2654435761u;
			state ^= static_cast<uint32_t>(sampleIndex * 3266489917u);
			state = state * 1664525u + 1013904223u;
			state ^= state >> 15;
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(static_cast<int32_t>(state)) * (0.5f / 2147483648.0f) * options_.outputScale;
		}

		// The bytes that arrived in one opened input (index into the
		// input list given to openChannels).
		const std::vector<unsigned char>& inputRecord(size_t openedInput) const
		{
			return inputRecords_[openedInput];
		}

		unsigned long switches() const noexcept {return switches_;}
		unsigned long timeInfoSwitches() const noexcept {return timeInfoSwitches_;}
		unsigned long resetRequests() const noexcept {return resetRequests_;}
		unsigned long latencyChanges() const noexcept {return latencyChanges_;}
		double lastRateChange() const noexcept {return lastRateChange_;}
		unsigned long rateChanges() const noexcept {return rateChanges_;}
		const std::vector<ASIOBufferInfo>& infos() const noexcept {return infos_;}
		ASIOCallbacks* callbacks() noexcept {return &callbacks_;}

		// Optional: something to run inside every switch (a test stopping
		// the driver from the callback, for instance).
		void (*onSwitch)(HostStub&, long index) = nullptr;

	private:
		static HostStub* current_;

		static void bufferSwitchThunk(long index, ASIOBool direct)
		{
			if (current_ != nullptr)
				current_->onBufferSwitch(index, direct, false);
		}

		static ASIOTime* bufferSwitchTimeInfoThunk(ASIOTime* params, long index, ASIOBool direct)
		{
			if (current_ != nullptr)
				current_->onBufferSwitch(index, direct, true);
			return params;
		}

		static void sampleRateDidChangeThunk(ASIOSampleRate rate)
		{
			if (current_ != nullptr)
			{
				current_->rateChanges_++;
				current_->lastRateChange_ = rate;
			}
		}

		static long asioMessageThunk(long selector, long value, void*, double*)
		{
			if (current_ == nullptr)
				return 0;
			return current_->onAsioMessage(selector, value);
		}

		long onAsioMessage(long selector, long value)
		{
			switch (selector)
			{
			case kAsioSelectorSupported:
				if (value == kAsioSupportsTimeInfo)
					return options_.supportsTimeInfo ? 1 : 0;
				if (value == kAsioResetRequest)
					return options_.supportsResetRequest ? 1 : 0;
				if (value == kAsioEngineVersion || value == kAsioLatenciesChanged)
					return 1;
				return 0;
			case kAsioEngineVersion:
				return 2;
			case kAsioSupportsTimeInfo:
				return options_.supportsTimeInfo ? 1 : 0;
			case kAsioResetRequest:
				resetRequests_++;
				return options_.supportsResetRequest ? 1 : 0;
			case kAsioLatenciesChanged:
				latencyChanges_++;
				return 1;
			default:
				return 0;
			}
		}

		void onBufferSwitch(long index, ASIOBool, bool timeInfo)
		{
			switches_++;
			if (timeInfo)
				timeInfoSwitches_++;
			if (options_.proAudioCallback && switches_ == 1)
			{
				DWORD taskIndex = 0;
				HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
				if (task != nullptr)
					AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL);
			}
			if (frames_ <= 0)
				return;
			std::vector<float> samples(static_cast<size_t>(frames_));
			size_t inputSlot = 0;
			for (ASIOBufferInfo& info : infos_)
			{
				unsigned char* buffer = static_cast<unsigned char*>(info.buffers[index]);
				if (buffer == nullptr)
					continue;
				const size_t bytes = static_cast<size_t>(codec_.bytesPerSample) * static_cast<size_t>(frames_);
				if (info.isInput != ASIOFalse)
				{
					inputRecords_[inputSlot].insert(inputRecords_[inputSlot].end(), buffer, buffer + bytes);
					inputSlot++;
				}
				else
				{
					for (long n = 0; n < frames_; n++)
						samples[static_cast<size_t>(n)] = outputSample(info.channelNum, outputPosition_ + static_cast<uint64_t>(n));
					codec_.fromFloat(samples.data(), buffer, static_cast<unsigned>(frames_));
				}
			}
			outputPosition_ += static_cast<uint64_t>(frames_);
			if (onSwitch != nullptr)
				onSwitch(*this, index);
			if (options_.callOutputReady && driver_ != nullptr)
				driver_->outputReady();
		}

		Options options_;
		ASIOCallbacks callbacks_ = {};
		eapo::asio::SampleCodec codec_;
		IASIO* driver_ = nullptr;
		long frames_ = 0;
		std::vector<ASIOBufferInfo> infos_;
		std::vector<std::vector<unsigned char>> inputRecords_;
		uint64_t outputPosition_ = 0;
		unsigned long switches_ = 0;
		unsigned long timeInfoSwitches_ = 0;
		unsigned long resetRequests_ = 0;
		unsigned long latencyChanges_ = 0;
		unsigned long rateChanges_ = 0;
		double lastRateChange_ = 0.0;
	};

	inline HostStub* HostStub::current_ = nullptr;
}
