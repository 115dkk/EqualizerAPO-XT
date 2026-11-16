/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/StreamHost.h"

#include <atomic>
#include <thread>

#include "asio/EngineHostCore.h"
#include "asio/HostControlServer.h"
#include "services/logging/Logging.h"

namespace eapo::asio
{
	struct StreamHost::ServeThread
	{
		std::atomic<bool> abandon{false};
		std::atomic<bool> finished{false};
		winutil::UniqueHandle mapping;
		winutil::UniqueMappedView base;
		winutil::UniqueHandle events[RingEvents::count];
		winutil::UniqueHandle producer;
		std::jthread thread;
	};

	StreamHost::StreamHost(HostActivity& activity, IRegistry& registry)
		: activity_(activity), registry_(registry)
	{
	}

	StreamHost::~StreamHost()
	{
		stop();
	}

	HostOpenReply StreamHost::open(HostOpenRequest request)
	{
		reap();
		HostOpenReply reply;
		reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
		reply.hostPid = GetCurrentProcessId();

		auto worker = std::make_unique<ServeThread>();
		worker->mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, request.ringName));
		DWORD openError = worker->mapping ? ERROR_SUCCESS : GetLastError();
		worker->base.reset(worker->mapping
			? MapViewOfFile(worker->mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, request.ringBytes) : nullptr);
		if (!worker->base && openError == ERROR_SUCCESS)
			openError = GetLastError();
		bool ok = static_cast<bool>(worker->base);
		for (unsigned i = 0; ok && i < RingEvents::count; i++)
		{
			worker->events[i].reset(OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
				HostNames::event(request.ringName, RingEvents::table[i].suffix).c_str()));
			ok = static_cast<bool>(worker->events[i]);
			if (!ok)
				openError = GetLastError();
		}
		if (!ok)
		{
			LogFStatic(L"ASIO host: the ring %s could not be opened (error %lu)", request.ringName, openError);
			return reply;
		}

		worker->producer.reset(OpenProcess(SYNCHRONIZE, FALSE, request.producerPid));
		if (!worker->producer)
		{
			LogFStatic(L"ASIO host: the producer process %u of %s could not be opened (error %lu); serving without a liveness watch",
				request.producerPid, request.ringName, GetLastError());
		}
		ServeThread* raw = worker.get();
		workers_.push_back(std::move(worker));
		activity_.started();
		try
		{
			raw->thread = std::jthread([this, request, raw] {serve(request, *raw);});
		}
		catch (...)
		{
			activity_.finished();
			workers_.pop_back();
			throw;
		}
		reply.status = static_cast<uint32_t>(HostOpenStatus::Accepted);
		return reply;
	}

	void StreamHost::stop() noexcept
	{
		for (const std::unique_ptr<ServeThread>& worker : workers_)
		{
			worker->abandon.store(true, std::memory_order_release);
			worker->thread.request_stop();
		}
		workers_.clear();
	}

	void StreamHost::serve(HostOpenRequest request, ServeThread& worker) noexcept
	{
		ServeOptions options;
		options.configPath = request.configPath;
		options.proAudio = true;
		options.registry = &registry_;
		options.abandon = &worker.abandon;
		EngineHostCore::attachAndServe(worker.base.get(), request.ringBytes,
			RingEvents::toSync(worker.events, worker.producer.get()), options, GetCurrentProcessId());
		activity_.finished();
		worker.finished.store(true, std::memory_order_release);
	}

	void StreamHost::reap()
	{
		for (auto it = workers_.begin(); it != workers_.end();)
		{
			if ((*it)->finished.load(std::memory_order_acquire))
				it = workers_.erase(it);
			else
				++it;
		}
	}
}
