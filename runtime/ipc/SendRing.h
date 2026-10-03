/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace eapo::ipc::send
{
	constexpr uint32_t magic = 0x53504145u; // 'EAPS'
	constexpr uint32_t layoutVersion = 1;
	constexpr uint32_t maxChannels = 16;
	constexpr uint32_t capacityFrames = 32768;
	constexpr size_t nameChars = 32;
	constexpr size_t headerBytes = 2048;
	constexpr size_t planeBytes = size_t(capacityFrames) * sizeof(float);
	constexpr size_t regionBytes = headerBytes + maxChannels * planeBytes;

	enum class State : uint32_t
	{
		Empty = 0,
		Ready = 1,
		Closing = 2
	};

	enum class MixMode : uint32_t
	{
		Mix = 0,
		Replace = 1
	};

	// Fixed shared-memory layout. Sender settings occupy the first cache line
	// and the target-name area. The realtime sender cursor and receiver-only
	// diagnostics each have their own cache line.
	struct alignas(64) Header
	{
		volatile LONG magic;
		volatile LONG version;
		volatile LONG state;
		volatile LONG generation;
		volatile LONG64 senderId;
		double sampleRate;
		volatile LONG channelCount;
		volatile LONG capacity;
		volatile LONG delayFrames;
		volatile LONG senderMaxFrameCount;
		volatile LONG mixMode;
		volatile LONG64 qpcFrequency;

		wchar_t targetNames[maxChannels][nameChars];

		volatile LONG64 writePos;
		volatile LONG64 writeQpc;
		uint8_t writeReserved[48];

		volatile LONG64 readPos;
		volatile LONG64 underruns;
		volatile LONG64 driftSteps;
		volatile LONG64 attachedSenderId;
		uint8_t receiverReserved[32];

		uint8_t reserved[832];
	};

	static_assert(std::is_standard_layout_v<Header>, "Send ring header must have a stable layout");
	static_assert(sizeof(Header) == headerBytes, "Send ring header must be exactly 2048 bytes");
	static_assert(offsetof(Header, targetNames) == 64, "Target names begin on a cache line");
	static_assert(offsetof(Header, writePos) % 64 == 0, "Writer cursor begins on a cache line");
	static_assert(offsetof(Header, readPos) % 64 == 0, "Receiver diagnostics begin on a cache line");
	static_assert(offsetof(Header, writePos) != offsetof(Header, readPos),
		"Writer and receiver fields must not share a cache line");

	// canonicalEndpointGuid must already be lower case and enclosed in braces.
	// The prefix normally comes from EngineSetup::sendNamePrefix.
	std::wstring mappingName(const std::wstring& prefix, const std::wstring& canonicalEndpointGuid);
	std::wstring eventName(const std::wstring& prefix, const std::wstring& canonicalEndpointGuid);

	struct SendParams
	{
		uint64_t senderId = 0;
		double sampleRate = 0.0;
		uint32_t delayFrames = 0;
		uint32_t senderMaxFrameCount = 0;
		MixMode mixMode = MixMode::Mix;
		std::vector<std::wstring> targetNames;
	};

	class SendWriterCore
	{
	public:
		explicit SendWriterCore(void* region);

		bool otherSenderLive(uint64_t mySenderId, int64_t nowQpc, int64_t qpcFrequency) const;
		void publish(const SendParams& params, int64_t qpcFrequency);
		// Blocks longer than the ring are clipped to their newest capacityFrames
		// samples before writing and advancing the published cursor.
		void write(const float* const* planes, uint32_t channelCount, uint32_t frameCount,
			int64_t nowQpc) noexcept;
		void close();

	private:
		Header* header_;
	};

	struct AttachPlan
	{
		bool ok = false;
		std::wstring reason;
		uint64_t senderId = 0;
		uint32_t generation = 0;
		MixMode mixMode = MixMode::Mix;
		uint32_t channelCount = 0;
		int targetIndex[maxChannels] = {
			-1, -1, -1, -1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1, -1, -1, -1
		};
		int64_t readPos = 0;
		uint32_t delayFrames = 0;
		std::vector<std::wstring> ignoredNames;
	};

	AttachPlan prepareAttach(const void* region, double ownSampleRate, uint32_t ownMaxFrameCount,
		const std::vector<std::wstring>& deviceChannels, int64_t nowQpc);

	class SendReaderCore
	{
	public:
		SendReaderCore(const void* region, const AttachPlan& plan, double sampleRate,
			uint32_t ownMaxFrameCount);

		enum class Status
		{
			Delivered,
			Underrun,
			SenderGone
		};

		Status read(double* const* channels, uint32_t channelCount, uint32_t frameCount,
			int64_t nowQpc) noexcept;
		uint64_t underruns() const;
		uint64_t driftSteps() const;

	private:
		const Header* header_;
		Header* diagnostics_;
		uint64_t senderId_;
		uint32_t generation_;
		MixMode mixMode_;
		uint32_t ringChannelCount_;
		int targetIndex_[maxChannels];
		int64_t readPos_;
		uint32_t delayFrames_;
		int64_t qpcFrequency_;
		double sampleRate_;
		uint32_t ownMaxFrameCount_;
		uint64_t underruns_ = 0;
		uint64_t driftSteps_ = 0;
		uint32_t leadSamples_ = 0;
		double leadSum_ = 0.0;
		double leadAtAttach_ = 0.0;
		double averageLead_ = 0.0;
		bool senderGone_ = false;
	};
}
