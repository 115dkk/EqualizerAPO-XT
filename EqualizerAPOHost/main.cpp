/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	EqualizerAPOHost.exe: the engine host for ASIO streams
	(docs/architecture/asio-host-study.md, sections 9-10). One per session
	endpoint, started on demand by the first wrapper that needs it. The shared
	HostControlServer owns the named-pipe protocol and idle lifetime; this file
	injects the operation that maps one requested ring and starts its engine
	thread.
*/

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "EqualizerAPOHost/Keepalive.h"
#include "asio/EngineHostCore.h"
#include "asio/HostControlServer.h"
#include "asio/HostProtocol.h"
#include "runtime/ipc/StreamRing.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"
#include "vst/PluginResidency.h"

// After windows.h (through the ring header): shellapi.h needs its types.
#include <shellapi.h>

namespace
{
	struct Arguments
	{
		std::wstring endpoint;
		uint32_t lingerMs = 60000;
		bool resident = false;
	};

	Arguments parseArguments(int argc, wchar_t** argv)
	{
		Arguments a;
		for (int i = 1; i < argc; i++)
		{
			const std::wstring key = argv[i];
			if (key == L"--endpoint" && i + 1 < argc)
				a.endpoint = argv[++i];
			else if (key == L"--linger" && i + 1 < argc)
				a.lingerMs = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
			else if (key == L"--resident")
				a.resident = true;
		}
		if (a.endpoint.empty())
			a.endpoint = eapo::asio::HostNames::defaultEndpoint();
		return a;
	}

	class StreamHost
	{
	public:
		explicit StreamHost(eapo::asio::HostActivity& activity)
			: activity_(activity)
		{
		}

		~StreamHost()
		{
			stop();
		}

		eapo::asio::HostOpenReply open(eapo::asio::HostOpenRequest request)
		{
			reap();
			auto worker = std::make_unique<ServeThread>();
			ServeThread* raw = worker.get();
			activity_.started();
			try
			{
				raw->thread = std::jthread([this, request, raw] {serve(request, *raw);});
			}
			catch (...)
			{
				activity_.finished();
				throw;
			}
			workers_.push_back(std::move(worker));
			eapo::asio::HostOpenReply reply;
			reply.status = static_cast<uint32_t>(eapo::asio::HostOpenStatus::Accepted);
			reply.hostPid = GetCurrentProcessId();
			return reply;
		}

		void stop() noexcept
		{
			for (const std::unique_ptr<ServeThread>& worker : workers_)
			{
				worker->abandon.store(true, std::memory_order_release);
				worker->thread.request_stop();
			}
			workers_.clear();
		}

	private:
		struct ServeThread
		{
			std::atomic<bool> abandon{false};
			std::atomic<bool> finished{false};
			std::jthread thread;
		};

		void serve(eapo::asio::HostOpenRequest request, ServeThread& worker) noexcept
		{
			winutil::UniqueHandle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, request.ringName));
			winutil::UniqueMappedView base(mapping ? MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, request.ringBytes) : nullptr);
			winutil::UniqueHandle events[eapo::asio::RingEvents::count];
			bool ok = static_cast<bool>(base);
			for (unsigned i = 0; ok && i < eapo::asio::RingEvents::count; i++)
			{
				events[i].reset(OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
					eapo::asio::HostNames::event(request.ringName, eapo::asio::RingEvents::table[i].suffix).c_str()));
				ok = static_cast<bool>(events[i]);
			}
			if (!ok)
				LogFStatic(L"ASIO host: the ring %s could not be opened (error %lu)", request.ringName, GetLastError());
			winutil::UniqueHandle producer(OpenProcess(SYNCHRONIZE, FALSE, request.producerPid));
			if (ok && !producer)
			{
				LogFStatic(L"ASIO host: the producer process %u of %s could not be opened (error %lu); serving without a liveness watch",
					request.producerPid, request.ringName, GetLastError());
			}
			if (ok)
			{
				eapo::asio::ServeOptions options;
				options.configPath = request.configPath;
				options.proAudio = true;
				options.registry = &systemRegistry();
				options.abandon = &worker.abandon;
				eapo::asio::EngineHostCore::attachAndServe(base.get(), request.ringBytes,
					eapo::asio::RingEvents::toSync(events, producer.get()), options, GetCurrentProcessId());
			}
			activity_.finished();
			worker.finished.store(true, std::memory_order_release);
		}

		void reap()
		{
			for (auto it = workers_.begin(); it != workers_.end();)
			{
				if ((*it)->finished.load(std::memory_order_acquire))
					it = workers_.erase(it);
				else
					++it;
			}
		}

		eapo::asio::HostActivity& activity_;
		std::vector<std::unique_ptr<ServeThread>> workers_;
	};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	PluginResidency::keepLoadedForProcessLifetime(true);

	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	const Arguments arguments = parseArguments(argc, argv);
	if (argv != nullptr)
		LocalFree(argv);
	Logging::useUserFile(L"EqualizerAPOHost.log", false, false, false);

	winutil::UniqueHandle owner(CreateMutexW(nullptr, TRUE, eapo::asio::HostNames::owner(arguments.endpoint).c_str()));
	if (!owner)
		return 2;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;

	eapo::asio::HostActivity activity;
	StreamHost streams(activity);
	eapo::asio::HostControlOptions controlOptions;
	controlOptions.endpoint = arguments.endpoint;
	controlOptions.lingerMs = arguments.lingerMs;
	controlOptions.resident = arguments.resident;
	eapo::asio::HostControlServer server(controlOptions, activity,
		// cppcheck-suppress throwInEntryPoint ; HostControlServer calls this handler inside catch (...) and answers the client with a failure reply
		[&streams](eapo::asio::HostOpenRequest request) {return streams.open(request);});

	int result;
	{
		// Held for its lifetime only: the keepalive thread runs while the server
		// does and is stopped and joined when this scope ends.
		// cppcheck-suppress unreadVariable
		std::jthread keepalive = arguments.resident ? startKeepalive() : std::jthread();
		result = server.run();
	}
	streams.stop();
	ReleaseMutex(owner.get());
	return result;
}
