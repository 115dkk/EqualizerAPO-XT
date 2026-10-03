/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "runtime/ipc/SendRing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <sstream>

#include "audio/ChannelLayout.h"
#include "runtime/memory/AlignedMemory.h"

namespace eapo::ipc::send
{
	namespace
	{
		uint32_t readWord(const volatile LONG* value) noexcept
		{
			return static_cast<uint32_t>(ReadAcquire(value));
		}

		uint64_t readWord64(const volatile LONG64* value) noexcept
		{
			return static_cast<uint64_t>(ReadAcquire64(value));
		}

		int64_t readSigned64(const volatile LONG64* value) noexcept
		{
			return static_cast<int64_t>(ReadAcquire64(value));
		}

		void writeWord(volatile LONG* destination, uint32_t value) noexcept
		{
			WriteRelease(destination, static_cast<LONG>(value));
		}

		void writeWord64(volatile LONG64* destination, uint64_t value) noexcept
		{
			WriteRelease64(destination, static_cast<LONG64>(value));
		}

		void writeSigned64(volatile LONG64* destination, int64_t value) noexcept
		{
			WriteRelease64(destination, static_cast<LONG64>(value));
		}

		float* plane(Header* header, uint32_t channel) noexcept
		{
			auto* bytes = reinterpret_cast<unsigned char*>(header);
			return reinterpret_cast<float*>(bytes + headerBytes + size_t(channel) * planeBytes);
		}

		const float* plane(const Header* header, uint32_t channel) noexcept
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(header);
			return reinterpret_cast<const float*>(bytes + headerBytes + size_t(channel) * planeBytes);
		}

		size_t ringIndex(int64_t position) noexcept
		{
			return static_cast<size_t>(static_cast<uint64_t>(position) & (capacityFrames - 1));
		}

		std::wstring sampleRateMismatch(double senderRate, double receiverRate)
		{
			std::wostringstream stream;
			stream << L"sample rate " << senderRate << L" differs from this endpoint's " << receiverRate;
			return stream.str();
		}

		bool fresh(int64_t stamp, int64_t nowQpc, int64_t qpcFrequency) noexcept
		{
			if (qpcFrequency <= 0)
				return false;
			const int64_t age = nowQpc - stamp;
			return age >= 0 && age < qpcFrequency;
		}

		struct WriteCursor
		{
			int64_t qpc = 0;
			int64_t position = 0;
		};

		WriteCursor readWriteCursor(const Header* header) noexcept
		{
			WriteCursor cursor;
			for (;;)
			{
				cursor.qpc = readSigned64(&header->writeQpc);
				cursor.position = readSigned64(&header->writePos);
				if (cursor.qpc == readSigned64(&header->writeQpc))
					return cursor;
			}
		}

		int64_t anchorPosition(const WriteCursor& cursor, int64_t nowQpc,
			double sampleRate, int64_t qpcFrequency, uint32_t delayFrames) noexcept
		{
			const double elapsedValue = static_cast<double>(nowQpc - cursor.qpc)
				* sampleRate / static_cast<double>(qpcFrequency);
			const int64_t elapsed = static_cast<int64_t>(std::llround(
				(std::clamp)(elapsedValue, 0.0, static_cast<double>(capacityFrames))));
			return cursor.position + elapsed - delayFrames;
		}
	}

	std::wstring mappingName(const std::wstring& prefix, const std::wstring& canonicalEndpointGuid)
	{
		return prefix + canonicalEndpointGuid;
	}

	std::wstring eventName(const std::wstring& prefix, const std::wstring& canonicalEndpointGuid)
	{
		return mappingName(prefix, canonicalEndpointGuid) + L".ready";
	}

	SendWriterCore::SendWriterCore(void* region)
		: header_(static_cast<Header*>(region))
	{
		assert(header_ != nullptr);
	}

	bool SendWriterCore::otherSenderLive(uint64_t mySenderId, int64_t nowQpc, int64_t qpcFrequency) const
	{
		if (readWord(&header_->state) != static_cast<uint32_t>(State::Ready))
			return false;
		const uint64_t senderId = readWord64(&header_->senderId);
		if (senderId == 0 || senderId == mySenderId)
			return false;
		return fresh(readSigned64(&header_->writeQpc), nowQpc, qpcFrequency);
	}

	void SendWriterCore::publish(const SendParams& params, int64_t qpcFrequency)
	{
		assert(params.senderId != 0);
		assert(!params.targetNames.empty() && params.targetNames.size() <= maxChannels);
		assert(qpcFrequency > 0);

		const uint64_t previousSenderId = readWord64(&header_->senderId);
		const uint32_t nextGeneration = readWord(&header_->generation) + 1;
		const bool sameSender = previousSenderId == params.senderId;
		writeWord(&header_->state, static_cast<uint32_t>(State::Empty));

		const uint32_t channelCount = static_cast<uint32_t>((std::min)(params.targetNames.size(), size_t(maxChannels)));
		writeWord(&header_->magic, magic);
		writeWord(&header_->version, layoutVersion);
		writeWord64(&header_->senderId, params.senderId);
		header_->sampleRate = params.sampleRate;
		writeWord(&header_->channelCount, channelCount);
		writeWord(&header_->capacity, capacityFrames);
		writeWord(&header_->delayFrames, params.delayFrames);
		writeWord(&header_->senderMaxFrameCount, params.senderMaxFrameCount);
		writeWord(&header_->mixMode, static_cast<uint32_t>(params.mixMode));

		std::memset(header_->targetNames, 0, sizeof(header_->targetNames));
		for (uint32_t channel = 0; channel < channelCount; channel++)
		{
			const std::wstring& name = params.targetNames[channel];
			assert(name.size() < nameChars);
			const size_t count = (std::min)(name.size(), nameChars - 1);
			std::wmemcpy(header_->targetNames[channel], name.data(), count);
			header_->targetNames[channel][count] = L'\0';
		}
		writeWord(&header_->generation, nextGeneration);
		writeSigned64(&header_->qpcFrequency, qpcFrequency);

		if (!sameSender)
		{
			writeSigned64(&header_->writePos, 0);
			writeSigned64(&header_->writeQpc, 0);
		}
		writeWord(&header_->state, static_cast<uint32_t>(State::Ready));
		publishedSenderId_.store(params.senderId, std::memory_order_release);
	}

	#pragma AVRT_CODE_BEGIN
	void SendWriterCore::write(const float* const* planes, uint32_t channelCount, uint32_t frameCount,
		int64_t nowQpc) noexcept
	{
		const uint64_t publishedSenderId = publishedSenderId_.load(std::memory_order_acquire);
		if (publishedSenderId == 0 || readWord64(&header_->senderId) != publishedSenderId)
			return;

		const uint32_t publishedChannelCount = readWord(&header_->channelCount);
		assert(channelCount == publishedChannelCount);
		if (planes == nullptr || channelCount != publishedChannelCount || channelCount == 0)
			return;

		uint32_t sourceOffset = 0;
		if (frameCount > capacityFrames)
		{
			sourceOffset = frameCount - capacityFrames;
			frameCount = capacityFrames;
		}

		const int64_t writePos = readSigned64(&header_->writePos);
		const size_t firstIndex = ringIndex(writePos);
		const uint32_t firstCount = (std::min)(frameCount,
			capacityFrames - static_cast<uint32_t>(firstIndex));
		const uint32_t secondCount = frameCount - firstCount;
		for (uint32_t channel = 0; channel < channelCount; channel++)
		{
			assert(planes[channel] != nullptr);
			if (planes[channel] == nullptr)
				continue;
			float* destination = plane(header_, channel);
			std::memcpy(destination + firstIndex, planes[channel] + sourceOffset,
				size_t(firstCount) * sizeof(float));
			if (secondCount != 0)
			{
				std::memcpy(destination, planes[channel] + sourceOffset + firstCount,
					size_t(secondCount) * sizeof(float));
			}
		}
		writeSigned64(&header_->writePos, writePos + frameCount);
		writeSigned64(&header_->writeQpc, nowQpc);
	}
	#pragma AVRT_CODE_END

	void SendWriterCore::close()
	{
		writeWord(&header_->state, static_cast<uint32_t>(State::Closing));
	}

	AttachPlan prepareAttach(const void* region, double ownSampleRate, uint32_t ownMaxFrameCount,
		const std::vector<std::wstring>& deviceChannels, int64_t nowQpc)
	{
		AttachPlan plan;
		if (region == nullptr)
		{
			plan.reason = L"ring memory is unavailable";
			return plan;
		}

		const auto* header = static_cast<const Header*>(region);
		if (readWord(&header->magic) != magic)
		{
			plan.reason = L"ring magic mismatch";
			return plan;
		}
		if (readWord(&header->version) != layoutVersion)
		{
			plan.reason = L"ring layout version mismatch";
			return plan;
		}
		if (readWord(&header->state) != static_cast<uint32_t>(State::Ready))
		{
			plan.reason = L"sender is not ready";
			return plan;
		}

		const uint64_t senderId = readWord64(&header->senderId);
		const uint32_t generation = readWord(&header->generation);
		const double senderSampleRate = header->sampleRate;
		if (senderSampleRate != ownSampleRate)
		{
			plan.reason = sampleRateMismatch(senderSampleRate, ownSampleRate);
			return plan;
		}

		const uint32_t channelCount = readWord(&header->channelCount);
		const MixMode mixMode = static_cast<MixMode>(readWord(&header->mixMode));
		if (channelCount < 1 || channelCount > maxChannels)
		{
			plan.reason = L"invalid channel count";
			return plan;
		}
		if (readWord(&header->capacity) != capacityFrames)
		{
			plan.reason = L"ring capacity mismatch";
			return plan;
		}
		const uint32_t delayFrames = readWord(&header->delayFrames);
		const uint64_t reserve = uint64_t(ownMaxFrameCount) * 2;
		if (reserve >= capacityFrames || delayFrames >= capacityFrames - reserve)
		{
			plan.reason = L"latency too large for the ring";
			return plan;
		}
		const int64_t qpcFrequency = readSigned64(&header->qpcFrequency);
		if (qpcFrequency <= 0)
		{
			plan.reason = L"invalid QPC frequency";
			return plan;
		}

		wchar_t names[maxChannels][nameChars] = {};
		for (uint32_t channel = 0; channel < channelCount; channel++)
		{
			std::wmemcpy(names[channel], header->targetNames[channel], nameChars);
			names[channel][nameChars - 1] = L'\0';
		}

		const WriteCursor cursor = readWriteCursor(header);

		if (readWord(&header->state) != static_cast<uint32_t>(State::Ready)
			|| readWord64(&header->senderId) != senderId
			|| readWord(&header->generation) != generation)
		{
			plan.reason = L"sender changed while attaching";
			return plan;
		}

		plan.senderId = senderId;
		plan.generation = generation;
		plan.mixMode = mixMode;
		plan.channelCount = channelCount;
		plan.anchored = cursor.qpc != 0 && fresh(cursor.qpc, nowQpc, qpcFrequency);
		plan.readPos = anchorPosition(cursor, nowQpc, senderSampleRate, qpcFrequency, delayFrames);
		plan.delayFrames = delayFrames;
		for (uint32_t channel = 0; channel < channelCount; channel++)
		{
			const std::wstring name(names[channel]);
			const int index = name.empty()
				? -1
				: ChannelLayout::getChannelIndex(name, deviceChannels, true);
			if (index < 0 || static_cast<size_t>(index) >= deviceChannels.size())
			{
				plan.targetIndex[channel] = -1;
				plan.ignoredNames.push_back(name);
			}
			else
			{
				plan.targetIndex[channel] = index;
			}
		}
		plan.ok = true;
		return plan;
	}

	SendReaderCore::SendReaderCore(const void* region, const AttachPlan& plan, double sampleRate,
		uint32_t ownMaxFrameCount)
		: header_(static_cast<const Header*>(region)),
		  diagnostics_(const_cast<Header*>(static_cast<const Header*>(region))),
		  senderId_(plan.senderId),
		  generation_(plan.generation),
		  mixMode_(plan.mixMode),
		  ringChannelCount_(plan.channelCount),
		  readPos_(plan.readPos),
		  delayFrames_(plan.delayFrames),
		  qpcFrequency_(region == nullptr ? 0 : readSigned64(&header_->qpcFrequency)),
		  sampleRate_(sampleRate),
		  ownMaxFrameCount_(ownMaxFrameCount),
		  anchored_(plan.anchored)
	{
		assert(region != nullptr);
		assert(plan.ok);
		std::copy_n(plan.targetIndex, maxChannels, targetIndex_);
		if (diagnostics_ != nullptr)
		{
			writeSigned64(&diagnostics_->readPos, readPos_);
			writeWord64(&diagnostics_->underruns, 0);
			writeWord64(&diagnostics_->driftSteps, 0);
			writeWord64(&diagnostics_->attachedSenderId, senderId_);
		}
	}

	#pragma AVRT_CODE_BEGIN
	SendReaderCore::Status SendReaderCore::read(double* const* channels, uint32_t channelCount,
		uint32_t frameCount, int64_t nowQpc) noexcept
	{
		if (senderGone_ || header_ == nullptr
			|| readWord(&header_->state) != static_cast<uint32_t>(State::Ready)
			|| readWord(&header_->generation) != generation_
			|| readWord64(&header_->senderId) != senderId_)
		{
			senderGone_ = true;
			return Status::SenderGone;
		}

		const WriteCursor cursor = readWriteCursor(header_);
		if (cursor.qpc == 0 || !fresh(cursor.qpc, nowQpc, qpcFrequency_))
		{
			anchored_ = false;
			return Status::Idle;
		}

		auto resetLeadStatistics = [this]() noexcept {
			leadSamples_ = 0;
			leadSum_ = 0.0;
			leadAtAttach_ = 0.0;
			averageLead_ = 0.0;
		};
		auto reanchor = [this, &cursor, nowQpc, &resetLeadStatistics]() noexcept {
			readPos_ = anchorPosition(cursor, nowQpc, sampleRate_, qpcFrequency_, delayFrames_);
			resetLeadStatistics();
			anchored_ = true;
		};
		if (!anchored_)
			reanchor();

		int64_t available = cursor.position - readPos_;
		bool underrun = false;
		const uint32_t lapLimit = frameCount < capacityFrames ? capacityFrames - frameCount : 0;
		if (available > static_cast<int64_t>(lapLimit))
		{
			readPos_ = cursor.position - delayFrames_;
			available = cursor.position - readPos_;
			underrun = true;
		}
		else if (available <= -static_cast<int64_t>(frameCount))
		{
			reanchor();
			available = cursor.position - readPos_;
			underrun = true;
		}
		const int64_t leadBeforeRead = available;

		const uint32_t availableFrames = available <= 0 ? 0
			: static_cast<uint32_t>((std::min)(available,
				static_cast<int64_t>((std::min)(frameCount, capacityFrames))));
		if (availableFrames < frameCount)
			underrun = true;

		if (channels != nullptr)
		{
			for (uint32_t ringChannel = 0; ringChannel < ringChannelCount_; ringChannel++)
			{
				const int target = targetIndex_[ringChannel];
				if (target < 0 || static_cast<uint32_t>(target) >= channelCount
					|| channels[target] == nullptr)
				{
					continue;
				}
				double* destination = channels[target];
				const float* source = plane(header_, ringChannel);
				for (uint32_t frame = 0; frame < availableFrames; frame++)
				{
					const double sample = static_cast<double>(source[ringIndex(readPos_ + frame)]);
					if (mixMode_ == MixMode::Replace)
						destination[frame] = sample;
					else
						destination[frame] += sample;
				}
				if (mixMode_ == MixMode::Replace)
					std::fill(destination + availableFrames, destination + frameCount, 0.0);
			}
		}

		if (leadSamples_ < 50)
		{
			leadSum_ += static_cast<double>(leadBeforeRead);
			leadSamples_++;
			if (leadSamples_ == 50)
			{
				leadAtAttach_ = leadSum_ / 50.0;
				averageLead_ = leadAtAttach_;
			}
		}
		else if (sampleRate_ > 0.0 && frameCount != 0)
		{
			const double alpha = (std::min)(1.0,
				static_cast<double>(frameCount) / (2.0 * sampleRate_));
			averageLead_ += alpha * (static_cast<double>(leadBeforeRead) - averageLead_);
			const double threshold = static_cast<double>(ownMaxFrameCount_) / 2.0;
			if (averageLead_ > leadAtAttach_ + threshold)
			{
				readPos_++;
				driftSteps_++;
			}
			else if (averageLead_ < leadAtAttach_ - threshold)
			{
				readPos_--;
				driftSteps_++;
			}
		}

		readPos_ += frameCount;
		if (underrun)
			underruns_++;
		writeSigned64(&diagnostics_->readPos, readPos_);
		writeWord64(&diagnostics_->underruns, underruns_);
		writeWord64(&diagnostics_->driftSteps, driftSteps_);
		writeWord64(&diagnostics_->attachedSenderId, senderId_);
		return underrun ? Status::Underrun : Status::Delivered;
	}
	#pragma AVRT_CODE_END

	uint64_t SendReaderCore::underruns() const
	{
		return diagnostics_ == nullptr ? 0 : readWord64(&diagnostics_->underruns);
	}

	uint64_t SendReaderCore::driftSteps() const
	{
		return diagnostics_ == nullptr ? 0 : readWord64(&diagnostics_->driftSteps);
	}
}
