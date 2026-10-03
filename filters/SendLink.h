/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/IInputTap.h"
#include "runtime/ipc/SendRing.h"
#include "runtime/ipc/SendRingWin32.h"

struct SendReceiverNews
{
	bool attached = false;
	uint64_t senderId = 0;
	uint32_t channelCount = 0;
	std::vector<std::wstring> targetNames;
	std::vector<std::wstring> ignoredNames;
	std::wstring refusal;
	bool senderLeft = false;
	bool underrunsChanged = false;
	uint64_t underruns = 0;
};

class SendWriter
{
public:
	SendWriter(const std::wstring& prefix, const std::wstring& targetGuid);
	~SendWriter();

	SendWriter(const SendWriter&) = delete;
	SendWriter& operator=(const SendWriter&) = delete;

	uint64_t senderId() const noexcept {return senderId_;}
	bool otherSenderLive() const noexcept;
	void publishIfChanged(const eapo::ipc::send::SendParams& params);
	void close() noexcept;
	void write(const float* const* planes, uint32_t channelCount, uint32_t frameCount,
		uint64_t blockToken) noexcept;

	void markUnused() noexcept {usedInLoad_ = false; publishedThisLoad_ = false;}
	void markUsed() noexcept {usedInLoad_ = true;}
	bool usedInLoad() const noexcept {return usedInLoad_;}

private:
	bool ownsPublishedRing() const noexcept;

	eapo::ipc::send::SendRingWin32 ring_;
	eapo::ipc::send::SendWriterCore core_;
	eapo::ipc::send::SendParams lastParams_;
	uint64_t senderId_ = 0;
	uint64_t lastToken_ = UINT64_MAX;
	bool hasLastParams_ = false;
	bool publishedThisLoad_ = false;
	std::atomic<bool> closed_{true};
	bool usedInLoad_ = false;
};

class SendReceiver : public IInputTap
{
public:
	SendReceiver();
	~SendReceiver() override;

	SendReceiver(const SendReceiver&) = delete;
	SendReceiver& operator=(const SendReceiver&) = delete;

	void reset();
	void configure(const std::wstring& prefix, const std::wstring& ownGuid,
		double sampleRate, uint32_t maxFrameCount,
		const std::vector<std::wstring>& deviceChannelNames);
	bool configured() const noexcept {return configured_;}
	const std::wstring& readyEventName() const noexcept {return readyEventName_;}
	void tryAttach();
	SendReceiverNews takeNews();

	void apply(double* const* channels, unsigned channelCount, unsigned frameCount,
		uint64_t blockToken) noexcept override;
	bool mayAddAudio() const noexcept override;

private:
	struct Attachment;

	void publishAttachment(std::unique_ptr<Attachment> attachment);
	void clearAttachment(bool senderLeft);
	void cleanRetired();

	std::shared_ptr<eapo::ipc::send::SendRingWin32> ring_;
	std::unique_ptr<Attachment> currentOwner_;
	std::vector<std::unique_ptr<Attachment>> retired_;
	std::atomic<Attachment*> current_{nullptr};
	std::atomic<Attachment*> observed_{nullptr};

	std::wstring readyEventName_;
	double sampleRate_ = 0.0;
	uint32_t maxFrameCount_ = 0;
	std::vector<std::wstring> deviceChannelNames_;
	bool configured_ = false;

	bool pendingAttached_ = false;
	uint64_t pendingSenderId_ = 0;
	uint32_t pendingChannelCount_ = 0;
	std::vector<std::wstring> pendingTargetNames_;
	std::vector<std::wstring> pendingIgnoredNames_;
	std::wstring pendingRefusal_;
	std::wstring lastRefusal_;
	std::atomic<bool> senderLeftNews_{false};
	std::atomic<uint64_t> totalUnderruns_{0};
	uint64_t reportedUnderruns_ = 0;
	std::atomic<bool> mayAdd_{false};

	// Audio-thread-only state. A configuration crossfade calls apply twice with
	// one token; retaining the first attachment here reuses exactly the samples
	// read for that token even if the control thread publishes an attachment in
	// between the two calls.
	Attachment* blockAttachment_ = nullptr;
	uint64_t lastToken_ = UINT64_MAX;
	uint32_t blockFrameCount_ = 0;
	bool blockHasAudio_ = false;
};
