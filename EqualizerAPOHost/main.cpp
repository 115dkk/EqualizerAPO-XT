/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	EqualizerAPOHost.exe: the engine host for ASIO streams
	(docs/architecture/asio-host-study.md, sections 9-10). One per session
	endpoint, started on demand by the first wrapper that needs it, serving
	one FilterEngine pair per stream on its own Pro Audio thread, and leaving
	`--linger` milliseconds after the last stream ends so a DAW's buffer-size
	change does not respawn it. With `--resident` (the Device Selector's
	start-at-boot option writes a Run value with it) it never leaves on idle.

	Control: a message-mode named pipe. A wrapper writes one HostOpenRequest
	naming the ring it mapped; the host opens the ring and its events by
	name, answers with its pid, and serves. Everything after that goes
	through the ring; the pipe connection is closed.

	No window, no console: it logs to EqualizerAPOHost.log under the user's
	EqualizerAPO log folder, next to the Editor's.
*/

#include <atomic>
#include <cstring>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "asio/EngineHostCore.h"
#include "asio/HostProtocol.h"
#include "platform/windows/NamedPipeSecurity.h"
#include "runtime/ipc/StreamRing.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"

// After windows.h (through the ring header): shellapi.h needs its types.
#include <shellapi.h>

using eapo::asio::HostOpenReply;
using eapo::asio::HostOpenRequest;
using eapo::asio::HostOpenStatus;

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

	struct Server
	{
		Arguments arguments;
		struct ServeThread
		{
			std::atomic<bool> abandon{false};
			std::atomic<bool> finished{false};
			std::jthread thread;
		};

		std::atomic<int> activeStreams{0};
		std::atomic<ULONGLONG> idleSince{0};
		std::atomic<bool> stopping{false};
		std::vector<std::unique_ptr<ServeThread>> serveThreads;

		~Server()
		{
			stopServeThreads();
		}

		static bool overlappedIo(HANDLE pipe, bool write, void* buffer, DWORD bytes, std::stop_token stop = {})
		{
			winutil::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!event)
				return false;
			OVERLAPPED overlapped = {};
			overlapped.hEvent = event.get();
			DWORD transferred = 0;
			BOOL ok = write ? WriteFile(pipe, buffer, bytes, &transferred, &overlapped) : ReadFile(pipe, buffer, bytes, &transferred, &overlapped);
			if (!ok && GetLastError() == ERROR_IO_PENDING)
			{
				const ULONGLONG deadline = GetTickCount64() + 5000;
				for (;;)
				{
					const DWORD waited = WaitForSingleObject(overlapped.hEvent, 50);
					if (waited == WAIT_OBJECT_0)
					{
						ok = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
						break;
					}
					if (waited == WAIT_FAILED || stop.stop_requested() || GetTickCount64() >= deadline)
					{
						CancelIoEx(pipe, &overlapped);
						GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
						ok = FALSE;
						break;
					}
				}
			}
			return ok && transferred == bytes;
		}

		void serve(HostOpenRequest request, ServeThread& worker)
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
				// The stream still runs, but a producer that dies without
				// closing the ring now goes unnoticed: this stream's thread
				// keeps waiting for work, and the host counts it as active.
				LogFStatic(L"ASIO host: the producer process %u of %s could not be opened (error %lu); serving without a liveness watch",
					request.producerPid, request.ringName, GetLastError());
			}
			if (ok)
			{
				eapo::asio::ServeOptions options;
				options.configPath = request.configPath;
				options.proAudio = true;
				options.spinPeriods = 1.0;
				options.registry = &systemRegistry();
				options.abandon = &worker.abandon;
				eapo::asio::EngineHostCore::attachAndServe(base.get(), request.ringBytes,
					eapo::asio::RingEvents::toSync(events, producer.get()), options, GetCurrentProcessId());
			}
			producer.reset();
			for (winutil::UniqueHandle& event : events)
				event.reset();
			base.reset();
			mapping.reset();
			idleSince.store(GetTickCount64(), std::memory_order_release);
			activeStreams.fetch_sub(1, std::memory_order_release);
			worker.finished.store(true, std::memory_order_release);
		}

		void reapServeThreads()
		{
			for (auto it = serveThreads.begin(); it != serveThreads.end();)
			{
				if ((*it)->finished.load(std::memory_order_acquire))
					it = serveThreads.erase(it);
				else
					++it;
			}
		}

		void stopServeThreads()
		{
			for (const std::unique_ptr<ServeThread>& worker : serveThreads)
			{
				worker->abandon.store(true, std::memory_order_release);
				worker->thread.request_stop();
			}
			serveThreads.clear();
		}

		void handleConnection(HANDLE pipe)
		{
			HostOpenRequest request;
			HostOpenReply reply;
			reply.hostPid = GetCurrentProcessId();
			if (!overlappedIo(pipe, false, &request, sizeof(request)))
				return;
			if (request.version != eapo::asio::hostProtocolVersion)
				reply.status = static_cast<uint32_t>(HostOpenStatus::BadVersion);
			else if (stopping.load())
				reply.status = static_cast<uint32_t>(HostOpenStatus::ShuttingDown);
			else if (request.ringBytes < eapo::ipc::ringHeaderBytes || request.ringName[0] == L'\0')
				reply.status = static_cast<uint32_t>(HostOpenStatus::BadRing);
			else
				reply.status = static_cast<uint32_t>(HostOpenStatus::Accepted);
			request.ringName[127] = L'\0';
			request.configPath[259] = L'\0';
			if (reply.status == static_cast<uint32_t>(HostOpenStatus::Accepted))
			{
				if (request.lingerMs > arguments.lingerMs)
					arguments.lingerMs = request.lingerMs;
				reapServeThreads();
				std::unique_ptr<ServeThread> worker = std::make_unique<ServeThread>();
				ServeThread* raw = worker.get();
				activeStreams.fetch_add(1, std::memory_order_release);
				raw->thread = std::jthread([this, request, raw] {serve(request, *raw);});
				serveThreads.push_back(std::move(worker));
			}
			overlappedIo(pipe, true, &reply, sizeof(reply));
			FlushFileBuffers(pipe);
		}

		int run()
		{
			const std::wstring pipeName = eapo::asio::HostNames::pipe(arguments.endpoint);
			// This user and SYSTEM only, where the default descriptor let
			// everyone open the pipe for reading (audit #348 TD-46).
			winutil::pipes::PipeSecurity security(winutil::pipes::userOnlySddl(winutil::pipes::currentUserSid()));
			if (!security.valid())
			{
				LogFStatic(L"ASIO host: the control pipe's security descriptor could not be built (error %lu)", security.error());
				return 2;
			}
			const auto createInstance = [&](bool first) {
				return CreateNamedPipeW(pipeName.c_str(),
					PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
					PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
					sizeof(HostOpenReply), sizeof(HostOpenRequest), 0, security.attributes());
			};

			idleSince = GetTickCount64();
			LogFStatic(L"ASIO host: listening on %s, linger %u ms%s", pipeName.c_str(), arguments.lingerMs,
				arguments.resident ? L", resident" : L"");
			// The first instance has to be the first one of that name: a
			// program that took it earlier would otherwise receive the
			// wrappers' stream requests. The wrapper also checks who serves.
			winutil::UniqueHandle pipe(createInstance(true));
			if (!pipe)
			{
				const DWORD error = GetLastError();
				if (error == ERROR_ACCESS_DENIED)
					LogFStatic(L"ASIO host: another program already holds %s", pipeName.c_str());
				else
					LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", error);
				stopServeThreads();
				return 2;
			}
			for (;;)
			{
				winutil::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
				if (!event)
				{
					LogFStatic(L"ASIO host: the control pipe event could not be created (error %lu)", GetLastError());
					stopServeThreads();
					return 2;
				}
				OVERLAPPED overlapped = {};
				overlapped.hEvent = event.get();
				bool connected = ConnectNamedPipe(pipe.get(), &overlapped) != FALSE;
				const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();
				if (connectError == ERROR_PIPE_CONNECTED)
					connected = true;
				else if (!connected && connectError != ERROR_IO_PENDING)
				{
					LogFStatic(L"ASIO host: the control pipe could not accept a connection (error %lu)", connectError);
					stopServeThreads();
					return 2;
				}
				DWORD transferred = 0;
				while (!connected)
				{
					const DWORD waited = WaitForSingleObject(event.get(), 1000);
					if (waited == WAIT_OBJECT_0)
					{
						connected = GetOverlappedResult(pipe.get(), &overlapped, &transferred, FALSE) != FALSE;
						if (!connected)
						{
							LogFStatic(L"ASIO host: the control pipe connection failed (error %lu)", GetLastError());
							stopServeThreads();
							return 2;
						}
						break;
					}
					if (waited == WAIT_FAILED)
					{
						LogFStatic(L"ASIO host: waiting for the control pipe failed (error %lu)", GetLastError());
						CancelIoEx(pipe.get(), &overlapped);
						GetOverlappedResult(pipe.get(), &overlapped, &transferred, TRUE);
						stopServeThreads();
						return 2;
					}
					if (!arguments.resident && activeStreams.load() == 0 && GetTickCount64() - idleSince.load() >= arguments.lingerMs)
					{
						stopping = true;
						// Cancellation is asynchronous: drain it before the event or
						// its OVERLAPPED storage leaves this iteration.
						CancelIoEx(pipe.get(), &overlapped);
						GetOverlappedResult(pipe.get(), &overlapped, &transferred, TRUE);
						break;
					}
				}
				if (connected)
					handleConnection(pipe.get());
				// The next instance exists before this one closes, so the name
				// is never free for another program to take between two
				// wrappers.
				winutil::UniqueHandle next(stopping.load() ? INVALID_HANDLE_VALUE : createInstance(false));
				const DWORD nextError = GetLastError();
				DisconnectNamedPipe(pipe.get());
				pipe.reset();
				if (stopping.load())
					break;
				if (!next)
				{
					LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", nextError);
					stopServeThreads();
					return 2;
				}
				pipe = std::move(next);
			}
			stopServeThreads();
			LogFStatic(L"ASIO host: idle for %u ms, leaving", arguments.lingerMs);
			return 0;
		}
	};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	Server server;
	server.arguments = parseArguments(argc, argv);
	if (argv != nullptr)
		LocalFree(argv);
	Logging::useUserFile(L"EqualizerAPOHost.log", false, false, false);

	// One host per endpoint: a second start (two wrappers racing) leaves at
	// once and the first keeps serving.
	winutil::UniqueHandle owner(CreateMutexW(nullptr, TRUE, eapo::asio::HostNames::owner(server.arguments.endpoint).c_str()));
	if (!owner)
		return 2;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;
	const int result = server.run();
	ReleaseMutex(owner.get());
	return result;
}
