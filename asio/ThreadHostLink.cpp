/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/ThreadHostLink.h"

#include <cstring>
#include <malloc.h>

#include "asio/EngineHostCore.h"

namespace eapo::asio
{
	ThreadHostLink::ThreadHostLink(bool proAudio)
		: proAudio_(proAudio)
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
		for (int i = 0; i < 5; i++)
			events_[i] = CreateEventW(nullptr, i == 4 ? TRUE : FALSE, FALSE, nullptr);
		hostGone_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		producerGone_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);

		session.ringBase = region_;
		session.ringBytes = bytes;
		session.sync.work[0] = events_[0];
		session.sync.work[1] = events_[1];
		session.sync.done[0] = events_[2];
		session.sync.done[1] = events_[3];
		session.sync.ready = events_[4];
		session.sync.peer = hostGone_;
		session.hostPid = GetCurrentProcessId();

		eapo::ipc::RingSync consumerSync = session.sync;
		consumerSync.peer = producerGone_;
		kill_ = false;
		ServeOptions serve;
		serve.configPath = options.configPath;
		serve.proAudio = proAudio_;
		serve.spinPeriods = proAudio_ ? 1.0 : 0.0;
		serve.idleWaitMs = 100;
		void* base = region_;
		thread_ = std::thread([this, base, bytes, consumerSync, serve] {
			// The producer formats the header after open() returns; wait for
			// Announced before validating.
			eapo::ipc::RingHeader* header = static_cast<eapo::ipc::RingHeader*>(base);
			while (ReadAcquire(&header->state) == static_cast<LONG>(eapo::ipc::RingState::Empty) && !kill_.load())
			{
				if (WaitForSingleObject(consumerSync.peer, 5) == WAIT_OBJECT_0)
					break;
			}
			if (!kill_.load())
			{
				eapo::ipc::RingConsumer consumer(base, bytes, consumerSync);
				if (kill_.load())
					return;
				ServeOptions local = serve;
				local.abandon = &kill_;
				EngineHostCore::serveStream(consumer, local, GetCurrentProcessId());
			}
			SetEvent(hostGone_);
		});
		return true;
	}

	void ThreadHostLink::close(HostSession& session) noexcept
	{
		if (producerGone_ != nullptr)
			SetEvent(producerGone_);
		if (thread_.joinable())
			thread_.join();
		for (HANDLE& event : events_)
		{
			if (event != nullptr)
				CloseHandle(event);
			event = nullptr;
		}
		if (hostGone_ != nullptr)
			CloseHandle(hostGone_);
		if (producerGone_ != nullptr)
			CloseHandle(producerGone_);
		hostGone_ = nullptr;
		producerGone_ = nullptr;
		if (region_ != nullptr)
			_aligned_free(region_);
		region_ = nullptr;
		session = HostSession();
	}

	void ThreadHostLink::killHost() noexcept
	{
		kill_ = true;
	}
}
