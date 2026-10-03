/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "filters/SendLink.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using eapo::ipc::send::AttachPlan;
using eapo::ipc::send::Header;
using eapo::ipc::send::MixMode;
using eapo::ipc::send::SendParams;
using eapo::ipc::send::SendReaderCore;
using eapo::ipc::send::State;
using eapo::ipc::send::eventName;
using eapo::ipc::send::mappingName;
using eapo::ipc::send::maxChannels;
using eapo::ipc::send::prepareAttach;

namespace
{
	std::atomic<uint32_t> senderCounter{1};

	int64_t qpcNow() noexcept
	{
		LARGE_INTEGER counter = {};
		QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	int64_t qpcFrequency() noexcept
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		return frequency.QuadPart;
	}

	bool sameParams(const SendParams& left, const SendParams& right)
	{
		return left.senderId == right.senderId
			&& left.sampleRate == right.sampleRate
			&& left.delayFrames == right.delayFrames
			&& left.senderMaxFrameCount == right.senderMaxFrameCount
			&& left.mixMode == right.mixMode
			&& left.targetNames == right.targetNames;
	}

	uint64_t loadSenderId(const void* region) noexcept
	{
		if (region == nullptr)
			return 0;
		const auto* header = static_cast<const Header*>(region);
		return static_cast<uint64_t>(ReadAcquire64(&header->senderId));
	}

	State loadState(const void* region) noexcept
	{
		if (region == nullptr)
			return State::Empty;
		const auto* header = static_cast<const Header*>(region);
		return static_cast<State>(static_cast<uint32_t>(ReadAcquire(&header->state)));
	}
}

SendWriter::SendWriter(const std::wstring& prefix, const std::wstring& targetGuid)
	: ring_(mappingName(prefix, targetGuid), eventName(prefix, targetGuid)),
	  core_(ring_.region()),
	  senderId_((uint64_t(GetCurrentProcessId()) << 32)
		| static_cast<uint64_t>(senderCounter.fetch_add(1, std::memory_order_relaxed)))
{
	ring_.touchAllPages();
}

SendWriter::~SendWriter()
{
	close();
}

bool SendWriter::otherSenderLive() const noexcept
{
	if (!publishedThisLoad_ && !closed_.load(std::memory_order_acquire)
		&& ownsPublishedRing() && loadState(ring_.region()) == State::Ready)
	{
		return false;
	}
	return core_.otherSenderLive(senderId_, qpcNow(), qpcFrequency());
}

void SendWriter::publishIfChanged(const SendParams& params)
{
	if (!closed_.load(std::memory_order_acquire) && hasLastParams_ && sameParams(lastParams_, params)
		&& ownsPublishedRing() && loadState(ring_.region()) == State::Ready)
	{
		publishedThisLoad_ = true;
		return;
	}

	core_.publish(params, qpcFrequency());
	lastParams_ = params;
	hasLastParams_ = true;
	closed_.store(false, std::memory_order_release);
	publishedThisLoad_ = true;
	ring_.signal();
}

bool SendWriter::ownsPublishedRing() const noexcept
{
	return loadSenderId(ring_.region()) == senderId_;
}

void SendWriter::close() noexcept
{
	const bool wasClosed = closed_.exchange(true, std::memory_order_acq_rel);
	if (!wasClosed && ownsPublishedRing())
	{
		core_.close();
		ring_.signal();
	}
}

#pragma AVRT_CODE_BEGIN
void SendWriter::write(const float* const* planes, uint32_t channelCount, uint32_t frameCount,
	uint64_t blockToken) noexcept
{
	if (blockToken == lastToken_)
		return;
	lastToken_ = blockToken;
	core_.write(planes, channelCount, frameCount, qpcNow());
}
#pragma AVRT_CODE_END

struct SendReceiver::Attachment
{
	Attachment(std::shared_ptr<eapo::ipc::send::SendRingWin32> ring,
		const AttachPlan& plan, double sampleRate, uint32_t maxFrameCount)
		: ring(std::move(ring)),
		  reader(this->ring->region(), scratchPlan(plan), sampleRate, maxFrameCount),
		  realMixMode(plan.mixMode),
		  senderId(plan.senderId),
		  generation(plan.generation),
		  ringChannelCount(plan.channelCount),
		  scratchData(static_cast<size_t>(plan.channelCount) * maxFrameCount),
		  scratchPlanes(plan.channelCount)
	{
		std::copy_n(plan.targetIndex, maxChannels, targetIndex.data());
		for (uint32_t channel = 0; channel < ringChannelCount; channel++)
			scratchPlanes[channel] = scratchData.data() + static_cast<size_t>(channel) * maxFrameCount;
	}

	static AttachPlan scratchPlan(AttachPlan plan)
	{
		plan.mixMode = MixMode::Replace;
		for (uint32_t channel = 0; channel < maxChannels; channel++)
			plan.targetIndex[channel] = channel < plan.channelCount ? static_cast<int>(channel) : -1;
		return plan;
	}

	std::shared_ptr<eapo::ipc::send::SendRingWin32> ring;
	SendReaderCore reader;
	MixMode realMixMode = MixMode::Mix;
	uint64_t senderId = 0;
	uint32_t generation = 0;
	uint32_t ringChannelCount = 0;
	std::array<int, maxChannels> targetIndex{};
	std::vector<double> scratchData;
	std::vector<double*> scratchPlanes;
	std::atomic<bool> senderGone{false};
};

SendReceiver::SendReceiver() = default;

SendReceiver::~SendReceiver()
{
	current_.store(nullptr, std::memory_order_release);
	observed_.store(nullptr, std::memory_order_release);
	currentOwner_.reset();
	retired_.clear();
}

void SendReceiver::reset()
{
	configured_ = false;
	readyEventName_.clear();
	sampleRate_ = 0.0;
	maxFrameCount_ = 0;
	deviceChannelNames_.clear();
	pendingAttached_ = false;
	pendingSenderId_ = 0;
	pendingChannelCount_ = 0;
	pendingTargetNames_.clear();
	pendingIgnoredNames_.clear();
	pendingRefusal_.clear();
	lastRefusal_.clear();
	senderLeftNews_.store(false, std::memory_order_relaxed);
	totalUnderruns_.store(0, std::memory_order_relaxed);
	reportedUnderruns_ = 0;
	mayAdd_.store(false, std::memory_order_release);
	publishAttachment(nullptr);
	ring_.reset();
	cleanRetired();
}

void SendReceiver::configure(const std::wstring& prefix, const std::wstring& ownGuid,
	double sampleRate, uint32_t maxFrameCount,
	const std::vector<std::wstring>& deviceChannelNames)
{
	reset();
	sampleRate_ = sampleRate;
	maxFrameCount_ = maxFrameCount;
	deviceChannelNames_ = deviceChannelNames;
	readyEventName_ = eventName(prefix, ownGuid);
	ring_ = std::make_shared<eapo::ipc::send::SendRingWin32>(
		mappingName(prefix, ownGuid), readyEventName_);
	configured_ = true;
}

void SendReceiver::publishAttachment(std::unique_ptr<Attachment> attachment)
{
	if (currentOwner_)
		retired_.push_back(std::move(currentOwner_));
	currentOwner_ = std::move(attachment);
	Attachment* published = currentOwner_.get();
	current_.store(published, std::memory_order_release);
	mayAdd_.store(published != nullptr, std::memory_order_release);
	cleanRetired();
}

void SendReceiver::clearAttachment(bool senderLeft)
{
	if (current_.load(std::memory_order_acquire) == nullptr)
		return;
	publishAttachment(nullptr);
	if (senderLeft)
		senderLeftNews_.store(true, std::memory_order_release);
}

void SendReceiver::cleanRetired()
{
	const Attachment* const current = current_.load(std::memory_order_acquire);
	const Attachment* const observed = observed_.load(std::memory_order_acquire);
	// Once the audio thread has observed the latest non-null publication, it can
	// no longer be using any earlier attachment. A null publication is ambiguous
	// with the initial value, so those objects stay retired until the next attach
	// (or the receiver destructor).
	if (current != nullptr && observed == current)
		retired_.clear();
}

void SendReceiver::tryAttach()
{
	if (!configured_ || !ring_)
		return;

	cleanRetired();
	const AttachPlan plan = prepareAttach(ring_->region(), sampleRate_, maxFrameCount_,
		deviceChannelNames_, qpcNow());
	if (!plan.ok)
	{
		const bool waitingForSender = plan.reason == L"sender is not ready"
			|| plan.reason == L"ring magic mismatch";
		if (waitingForSender)
		{
			clearAttachment(plan.reason == L"sender is not ready"
				&& current_.load(std::memory_order_acquire) != nullptr);
			pendingRefusal_.clear();
			lastRefusal_.clear();
		}
		else
		{
			clearAttachment(false);
			if (plan.reason != lastRefusal_)
			{
				pendingRefusal_ = plan.reason;
				lastRefusal_ = plan.reason;
			}
		}
		return;
	}

	Attachment* const attached = current_.load(std::memory_order_acquire);
	if (attached != nullptr && attached->senderId == plan.senderId
		&& attached->generation == plan.generation
		&& !attached->senderGone.load(std::memory_order_acquire))
	{
		return;
	}

	auto prepared = std::make_unique<Attachment>(ring_, plan, sampleRate_, maxFrameCount_);
	pendingAttached_ = true;
	pendingSenderId_ = plan.senderId;
	pendingChannelCount_ = plan.channelCount;
	pendingIgnoredNames_ = plan.ignoredNames;
	pendingTargetNames_.clear();
	for (uint32_t channel = 0; channel < plan.channelCount; channel++)
	{
		if (plan.targetIndex[channel] >= 0)
			pendingTargetNames_.push_back(deviceChannelNames_[static_cast<size_t>(plan.targetIndex[channel])]);
	}
	pendingRefusal_.clear();
	lastRefusal_.clear();
	senderLeftNews_.store(false, std::memory_order_relaxed);
	publishAttachment(std::move(prepared));
}

SendReceiverNews SendReceiver::takeNews()
{
	cleanRetired();
	Attachment* const attached = current_.load(std::memory_order_acquire);
	if (attached != nullptr && attached->senderGone.load(std::memory_order_acquire))
		clearAttachment(true);

	SendReceiverNews news;
	news.attached = pendingAttached_;
	news.senderId = pendingSenderId_;
	news.channelCount = pendingChannelCount_;
	news.targetNames = std::move(pendingTargetNames_);
	news.ignoredNames = std::move(pendingIgnoredNames_);
	news.refusal = std::move(pendingRefusal_);
	news.senderLeft = senderLeftNews_.exchange(false, std::memory_order_acq_rel);
	news.underruns = totalUnderruns_.load(std::memory_order_acquire);
	news.underrunsChanged = news.underruns > reportedUnderruns_;
	if (news.underrunsChanged)
		reportedUnderruns_ = news.underruns;
	pendingAttached_ = false;
	pendingSenderId_ = 0;
	pendingChannelCount_ = 0;
	return news;
}

#pragma AVRT_CODE_BEGIN
void SendReceiver::apply(double* const* channels, unsigned channelCount, unsigned frameCount,
	uint64_t blockToken) noexcept
{
	Attachment* attachment = blockAttachment_;
	if (blockToken != lastToken_)
	{
		attachment = current_.load(std::memory_order_acquire);
		for (;;)
		{
			observed_.store(attachment, std::memory_order_release);
			Attachment* const confirmed = current_.load(std::memory_order_acquire);
			if (confirmed == attachment)
				break;
			attachment = confirmed;
		}
		lastToken_ = blockToken;
		blockAttachment_ = attachment;
		blockFrameCount_ = (std::min)(frameCount, maxFrameCount_);
		blockHasAudio_ = false;
		if (attachment != nullptr)
		{
			for (double* plane : attachment->scratchPlanes)
				std::fill_n(plane, blockFrameCount_, 0.0);

			const SendReaderCore::Status status = attachment->reader.read(
				attachment->scratchPlanes.data(),
				static_cast<uint32_t>(attachment->scratchPlanes.size()), blockFrameCount_, qpcNow());
			if (status == SendReaderCore::Status::SenderGone)
			{
				attachment->senderGone.store(true, std::memory_order_release);
				mayAdd_.store(false, std::memory_order_release);
				senderLeftNews_.store(true, std::memory_order_release);
			}
			else
			{
				blockHasAudio_ = true;
				if (status == SendReaderCore::Status::Underrun)
					totalUnderruns_.fetch_add(1, std::memory_order_release);
			}
		}
	}

	if (!blockHasAudio_ || attachment == nullptr)
		return;
	const unsigned appliedFrames = (std::min)(blockFrameCount_, frameCount);
	for (uint32_t ringChannel = 0; ringChannel < attachment->ringChannelCount; ringChannel++)
	{
		const int target = attachment->targetIndex[ringChannel];
		if (target < 0 || static_cast<unsigned>(target) >= channelCount)
			continue;
		double* const destination = channels[target];
		const double* const source = attachment->scratchPlanes[ringChannel];
		if (attachment->realMixMode == MixMode::Replace)
			std::copy_n(source, appliedFrames, destination);
		else
			for (unsigned frame = 0; frame < appliedFrames; frame++)
				destination[frame] += source[frame];
	}
}

bool SendReceiver::mayAddAudio() const noexcept
{
	return mayAdd_.load(std::memory_order_acquire);
}
#pragma AVRT_CODE_END
