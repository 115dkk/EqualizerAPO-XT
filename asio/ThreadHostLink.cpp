/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/ThreadHostLink.h"

#include <cstring>
#include <malloc.h>

#include "asio/EngineHostCore.h"
#include "asio/HostProtocol.h"

namespace eapo::asio
{
	void ThreadHostControl::killHost() noexcept
	{
		kill_.store(true, std::memory_order_release);
		holdHost(false);
	}

	void ThreadHostControl::holdHost(bool held) noexcept
	{
		const bool previous = held_.exchange(held, std::memory_order_acq_rel);
		if (previous == held)
			return;
		const HANDLE thread = thread_.load(std::memory_order_acquire);
		if (thread == nullptr)
			return;
		if (held)
			SuspendThread(thread);
		else
			ResumeThread(thread);
	}

	ThreadHostLink::ThreadHostLink(bool proAudio, uint32_t traceSlowUs, std::shared_ptr<ThreadHostControl> control)
		: control_(control != nullptr ? std::move(control) : std::make_shared<ThreadHostControl>()),
		proAudio_(proAudio), traceSlowUs_(traceSlowUs)
	{
	}

	ThreadHostLink::~ThreadHostLink()
	{
		close(nullptr);
	}

	std::unique_ptr<eapo::ipc::RingProducer> ThreadHostLink::open(const StreamFormat& format, const StreamOptions& options,
		OpenReport& report)
	{
		close(nullptr);
		const auto fail = [&](const char* message) -> std::unique_ptr<eapo::ipc::RingProducer> {
			report.status = OpenReport::Status::Unavailable;
			copyErrorMessage(report.message, message);
			close(nullptr);
			return nullptr;
		};
		const uint32_t bytes = eapo::ipc::RingGeometry::totalBytes(format);
		region_ = _aligned_malloc(bytes, eapo::ipc::ringAlignment);
		if (region_ == nullptr)
			return fail("the stream ring could not be allocated");
		std::memset(region_, 0, bytes);
		for (unsigned i = 0; i < RingEvents::count; i++)
		{
			events_[i].reset(CreateEventW(nullptr, RingEvents::table[i].manualReset ? TRUE : FALSE, FALSE, nullptr));
			if (!events_[i])
				return fail("the stream events could not be created");
		}
		hostGone_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!hostGone_)
			return fail("the host liveness event could not be created");
		producerGone_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!producerGone_)
			return fail("the producer liveness event could not be created");

		session_.ringBase = region_;
		session_.ringBytes = bytes;
		session_.sync = RingEvents::toSync(events_, hostGone_.get());
		session_.hostPid = GetCurrentProcessId();

		const eapo::ipc::RingSync consumerSync = RingEvents::toSync(events_, producerGone_.get());
		control_->kill_.store(false, std::memory_order_release);
		control_->held_.store(false, std::memory_order_release);
		ServeOptions serve;
		serve.configPath = options.configPath;
		serve.proAudio = proAudio_;
		serve.traceSlowUs = traceSlowUs_;
		serve.idleWaitMs = 100;
		serve.readyTimeoutMs = options.readyTimeoutMs;
		serve.abandon = &control_->kill_;
		void* base = region_;
		thread_ = std::thread([this, base, bytes, consumerSync, serve] {
			EngineHostCore::attachAndServe(base, bytes, consumerSync, serve, GetCurrentProcessId());
			SetEvent(hostGone_.get());
		});
		control_->thread_.store(thread_.native_handle(), std::memory_order_release);
		std::unique_ptr<eapo::ipc::RingProducer> producer = readyProducer(session_, format, options.readyTimeoutMs, report);
		if (producer == nullptr)
			close(nullptr);
		return producer;
	}

	void ThreadHostLink::close(std::unique_ptr<eapo::ipc::RingProducer> producer) noexcept
	{
		if (producer != nullptr)
		{
			producer->close();
			producer.reset();
		}
		control_->holdHost(false);
		if (producerGone_)
			SetEvent(producerGone_.get());
		if (thread_.joinable())
			thread_.join();
		control_->thread_.store(nullptr, std::memory_order_release);
		for (winutil::UniqueHandle& event : events_)
			event.reset();
		hostGone_.reset();
		producerGone_.reset();
		if (region_ != nullptr)
			_aligned_free(region_);
		region_ = nullptr;
		session_ = HostSession();
	}
}
