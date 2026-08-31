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
#include "runtime/ipc/StreamRing.h"
#include "services/logging/Logging.h"

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
			OVERLAPPED overlapped = {};
			overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (overlapped.hEvent == nullptr)
				return false;
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
			CloseHandle(overlapped.hEvent);
			return ok && transferred == bytes;
		}

		void serve(HostOpenRequest request, ServeThread& worker, std::stop_token stop)
		{
			HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, request.ringName);
			void* base = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, request.ringBytes) : nullptr;
			const wchar_t* const suffixes[5] = {L"work0", L"work1", L"done0", L"done1", L"ready"};
			HANDLE events[5] = {};
			bool ok = base != nullptr;
			for (int i = 0; ok && i < 5; i++)
			{
				events[i] = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, eapo::asio::HostNames::event(request.ringName, suffixes[i]).c_str());
				ok = events[i] != nullptr;
			}
			HANDLE producer = OpenProcess(SYNCHRONIZE, FALSE, request.producerPid);
			if (ok)
			{
				eapo::ipc::RingSync sync;
				sync.work[0] = events[0];
				sync.work[1] = events[1];
				sync.done[0] = events[2];
				sync.done[1] = events[3];
				sync.ready = events[4];
				sync.peer = producer;

				// HostOpenRequest has no readiness timeout. Match ThreadHostLink's
				// fixed 5 ms poll and StreamOptions' 20 second cold-start bound.
				eapo::ipc::RingHeader* header = static_cast<eapo::ipc::RingHeader*>(base);
				const ULONGLONG deadline = GetTickCount64() + 20000;
				while (ReadAcquire(&header->state) == static_cast<LONG>(eapo::ipc::RingState::Empty) && !stop.stop_requested())
				{
					if (producer != nullptr)
					{
						const DWORD waited = WaitForSingleObject(producer, 5);
						if (waited == WAIT_OBJECT_0)
							break;
						if (waited == WAIT_FAILED)
							Sleep(5);
					}
					else
					{
						Sleep(5);
					}
					if (GetTickCount64() >= deadline)
						break;
				}

				if (!stop.stop_requested() || ReadAcquire(&header->state) != static_cast<LONG>(eapo::ipc::RingState::Empty))
				{
					const bool validFormat = eapo::ipc::RingGeometry::validFormat(header->format);
					const uint64_t expectedBytes = validFormat ? eapo::ipc::RingGeometry::totalBytes(header->format) : 0;
					if (!validFormat || expectedBytes != header->totalBytes || expectedBytes > request.ringBytes)
					{
						WriteRelease(&header->faultCode, static_cast<LONG>(eapo::ipc::RingFault::LayoutMismatch));
						WriteRelease(&header->state, static_cast<LONG>(eapo::ipc::RingState::Fault));
						SetEvent(sync.ready);
					}
					else
					{
						eapo::ipc::RingConsumer consumer(base, request.ringBytes, sync);
						eapo::asio::ServeOptions options;
						options.configPath = request.configPath;
						options.proAudio = true;
						options.spinPeriods = 1.0;
						options.publishFacts = true;
						options.abandon = &worker.abandon;
						eapo::asio::EngineHostCore::serveStream(consumer, options, GetCurrentProcessId());
					}
				}
			}
			else
			{
				LogFStatic(L"ASIO host: the ring %s could not be opened (error %lu)", request.ringName, GetLastError());
			}
			if (producer != nullptr)
				CloseHandle(producer);
			for (HANDLE event : events)
			{
				if (event != nullptr)
					CloseHandle(event);
			}
			if (base != nullptr)
				UnmapViewOfFile(base);
			if (mapping != nullptr)
				CloseHandle(mapping);
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
				raw->thread = std::jthread([this, request, raw](std::stop_token stop) {serve(request, *raw, stop);});
				serveThreads.push_back(std::move(worker));
			}
			overlappedIo(pipe, true, &reply, sizeof(reply));
			FlushFileBuffers(pipe);
		}

		int run()
		{
			const std::wstring pipeName = eapo::asio::HostNames::pipe(arguments.endpoint);
			idleSince = GetTickCount64();
			LogFStatic(L"ASIO host: listening on %s, linger %u ms%s", pipeName.c_str(), arguments.lingerMs,
				arguments.resident ? L", resident" : L"");
			for (;;)
			{
				HANDLE pipe = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
					PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
					sizeof(HostOpenReply), sizeof(HostOpenRequest), 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					LogFStatic(L"ASIO host: the control pipe could not be created (error %lu)", GetLastError());
					stopServeThreads();
					return 2;
				}
				OVERLAPPED overlapped = {};
				overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				bool connected = false;
				if (ConnectNamedPipe(pipe, &overlapped) || GetLastError() == ERROR_PIPE_CONNECTED)
					connected = true;
				while (!connected)
				{
					const DWORD waited = WaitForSingleObject(overlapped.hEvent, 1000);
					if (waited == WAIT_OBJECT_0)
					{
						connected = true;
						break;
					}
					if (!arguments.resident && activeStreams.load() == 0 && GetTickCount64() - idleSince.load() >= arguments.lingerMs)
					{
						stopping = true;
						CancelIo(pipe);
						break;
					}
				}
				CloseHandle(overlapped.hEvent);
				if (connected)
					handleConnection(pipe);
				DisconnectNamedPipe(pipe);
				CloseHandle(pipe);
				if (stopping.load())
					break;
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
	HANDLE owner = CreateMutexW(nullptr, TRUE, eapo::asio::HostNames::owner(server.arguments.endpoint).c_str());
	if (owner == nullptr)
		return 2;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(owner);
		return 0;
	}
	const int result = server.run();
	ReleaseMutex(owner);
	CloseHandle(owner);
	return result;
}
