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
	ThreadHostLink::ThreadHostLink(bool proAudio, uint32_t traceSlowUs)
		: proAudio_(proAudio), traceSlowUs_(traceSlowUs)
	{
	}

	ThreadHostLink::~ThreadHostLink()
	{
		HostSession session;
		session.ringBase = region_;
		close(session);
	}

	bool ThreadHostLink::open(const StreamFormat& format, const StreamOptions& options, HostSession& session, std::string& error)
	{
		close(session);
		const uint32_t bytes = eapo::ipc::RingGeometry::totalBytes(format);
		region_ = _aligned_malloc(bytes, eapo::ipc::ringAlignment);
		if (region_ == nullptr)
		{
			error = "the stream ring could not be allocated";
			return false;
		}
		std::memset(region_, 0, bytes);
		for (unsigned i = 0; i < RingEvents::count; i++)
		{
			events_[i].reset(CreateEventW(nullptr, RingEvents::table[i].manualReset ? TRUE : FALSE, FALSE, nullptr));
			if (!events_[i])
			{
				error = "the stream events could not be created";
				close(session);
				return false;
			}
		}
		hostGone_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!hostGone_)
		{
			error = "the host liveness event could not be created";
			close(session);
			return false;
		}
		producerGone_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
		if (!producerGone_)
		{
			error = "the producer liveness event could not be created";
			close(session);
			return false;
		}

		session.ringBase = region_;
		session.ringBytes = bytes;
		session.sync = RingEvents::toSync(events_, hostGone_.get());
		session.hostPid = GetCurrentProcessId();

		const eapo::ipc::RingSync consumerSync = RingEvents::toSync(events_, producerGone_.get());
		kill_ = false;
		hold_ = false;
		ServeOptions serve;
		serve.configPath = options.configPath;
		serve.proAudio = proAudio_;
		serve.spinPeriods = proAudio_ ? 1.0 : 0.0;
		serve.traceSlowUs = traceSlowUs_;
		serve.idleWaitMs = 100;
		serve.readyTimeoutMs = options.readyTimeoutMs;
		serve.abandon = &kill_;
		serve.hold = &hold_;
		void* base = region_;
		thread_ = std::thread([this, base, bytes, consumerSync, serve] {
			EngineHostCore::attachAndServe(base, bytes, consumerSync, serve, GetCurrentProcessId());
			SetEvent(hostGone_.get());
		});
		return true;
	}

	void ThreadHostLink::close(HostSession& session) noexcept
	{
		if (producerGone_)
			SetEvent(producerGone_.get());
		if (thread_.joinable())
			thread_.join();
		for (winutil::UniqueHandle& event : events_)
			event.reset();
		hostGone_.reset();
		producerGone_.reset();
		if (region_ != nullptr)
			_aligned_free(region_);
		region_ = nullptr;
		session = HostSession();
	}

	void ThreadHostLink::killHost() noexcept
	{
		kill_ = true;
	}

	void ThreadHostLink::holdHost(bool held) noexcept
	{
		hold_ = held;
	}
}
