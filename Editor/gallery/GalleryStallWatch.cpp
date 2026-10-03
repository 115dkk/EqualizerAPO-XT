/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "GalleryStallWatch.h"

#if defined(_M_X64)
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <QDebug>
#include <QFileInfo>
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace
{
constexpr int maxFrames = 48;
constexpr int maxSamples = 400;
constexpr int maxThreads = 64;
constexpr size_t stackBytes = 1024 * 1024;

struct StackSample
{
	DWORD64 addresses[maxFrames] = {};
	int size = 0;
	int count = 0;
};

bool sameStack(const StackSample& left, const StackSample& right)
{
	return left.size == right.size
		&& std::equal(left.addresses, left.addresses + left.size, right.addresses);
}

LONGLONG counter()
{
	LARGE_INTEGER value;
	QueryPerformanceCounter(&value);
	return value.QuadPart;
}

ULONGLONG fileTime(const FILETIME& value)
{
	return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) + value.dwLowDateTime;
}

ULONGLONG threadCpuTime(HANDLE thread)
{
	FILETIME created, exited, kernel, user;
	if (!GetThreadTimes(thread, &created, &exited, &kernel, &user))
		return 0;
	return fileTime(kernel) + fileTime(user);
}

ULONGLONG difference(ULONGLONG end, ULONGLONG start)
{
	return end >= start ? end - start : 0;
}

struct Counters
{
	ULONGLONG guiCpu = 0;
	ULONGLONG processCpu = 0;
	ULONGLONG systemIdle = 0;
	ULONGLONG systemTotal = 0;
	PROCESS_MEMORY_COUNTERS_EX memory = {};
	IO_COUNTERS io = {};
	MEMORYSTATUSEX globalMemory = {};

	void read(HANDLE guiThread)
	{
		guiCpu = threadCpuTime(guiThread);
		FILETIME created, exited, kernel, user, idle;
		if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
			processCpu = fileTime(kernel) + fileTime(user);
		// GetSystemTimes includes idle in kernel; above 64 CPUs it covers the
		// calling thread's primary processor group, not all groups.
		if (GetSystemTimes(&idle, &kernel, &user))
		{
			systemIdle = fileTime(idle);
			systemTotal = fileTime(kernel) + fileTime(user);
		}
		memory.cb = sizeof(memory);
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
		GetProcessIoCounters(GetCurrentProcess(), &io);
		globalMemory.dwLength = sizeof(globalMemory);
		GlobalMemoryStatusEx(&globalMemory);
	}
};

double systemBusy(const Counters& first, const Counters& last)
{
	const ULONGLONG total = difference(last.systemTotal, first.systemTotal);
	const ULONGLONG idle = std::min(difference(last.systemIdle, first.systemIdle), total);
	return total != 0 ? 100.0 * (1.0 - static_cast<double>(idle) / static_cast<double>(total)) : 0.0;
}

void relocate(DWORD64& value, DWORD64 original, size_t copied, DWORD64 delta)
{
	if (value >= original && value - original < copied)
		value += delta;
}

void captureStack(HANDLE thread, StackSample& sample, DWORD64* buffer)
{
	CONTEXT context = {};
	context.ContextFlags = CONTEXT_FULL;
	size_t copied = 0;
	if (SuspendThread(thread) == static_cast<DWORD>(-1))
		return;

	// Only context/query/copy calls while suspended. In particular, unwind
	// metadata lookup can lock ntdll while the target owns the same lock.
	__try
	{
		MEMORY_BASIC_INFORMATION memory = {};
		if (GetThreadContext(thread, &context)
			&& VirtualQuery(reinterpret_cast<const void*>(context.Rsp), &memory, sizeof(memory)) != 0
			&& memory.State == MEM_COMMIT)
		{
			const size_t available = memory.RegionSize - (context.Rsp - reinterpret_cast<DWORD64>(memory.BaseAddress));
			const size_t bytes = std::min(available, stackBytes);
			// Retain completed chunks if the last copy faults.
			while (copied < bytes)
			{
				const size_t chunk = std::min(bytes - copied, size_t(4096));
				std::memcpy(reinterpret_cast<unsigned char*>(buffer) + copied,
					reinterpret_cast<const void*>(context.Rsp + copied), chunk);
				copied += chunk;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// An unreadable stack truncates this sample, not the gate.
	}
	ResumeThread(thread);

	if (copied < sizeof(DWORD64))
		return;
	const DWORD64 original = context.Rsp;
	const DWORD64 begin = reinterpret_cast<DWORD64>(buffer);
	const DWORD64 end = begin + copied;
	const DWORD64 delta = begin - original;
	relocate(context.Rsp, original, copied, delta);
	relocate(context.Rbp, original, copied, delta);
	relocate(context.Rbx, original, copied, delta);
	relocate(context.Rsi, original, copied, delta);
	relocate(context.Rdi, original, copied, delta);
	relocate(context.R12, original, copied, delta);
	relocate(context.R13, original, copied, delta);
	relocate(context.R14, original, copied, delta);
	relocate(context.R15, original, copied, delta);
	for (size_t i = 0; i < copied / sizeof(DWORD64); i++)
		relocate(buffer[i], original, copied, delta);

	__try
	{
		while (context.Rip != 0 && sample.size < maxFrames && context.Rsp >= begin && context.Rsp < end)
		{
			sample.addresses[sample.size++] = context.Rip;
			const DWORD64 previousRsp = context.Rsp;
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
			if (entry != nullptr)
			{
				PVOID handlerData = nullptr;
				DWORD64 establisherFrame = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry,
					&context, &handlerData, &establisherFrame, nullptr);
			}
			else
			{
				if (end - context.Rsp < sizeof(DWORD64))
					break;
				context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
				context.Rsp += sizeof(DWORD64);
			}
			if (context.Rsp <= previousRsp)
				break;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// The frames already collected are still useful.
	}
}

struct ThreadSnapshot
{
	HANDLE handle = nullptr;
	DWORD id = 0;
	ULONGLONG startCpu = 0;
	ULONGLONG cpu = 0;
	StackSample stack;
};

// Another process's CPU time, keyed by id and creation time so a reused id
// is not taken for the same process.
struct ProcessTimes
{
	DWORD id = 0;
	ULONGLONG created = 0;
	ULONGLONG cpu = 0;
	QString name;
};

ULONGLONG systemTimeNow()
{
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	return fileTime(now);
}

// Every other process this query right reaches, protected ones included. It
// allocates, so it runs only while no thread is suspended.
std::vector<ProcessTimes> readProcessTimes()
{
	std::vector<ProcessTimes> result;
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return result;
	const DWORD self = GetCurrentProcessId();
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry))
	{
		do
		{
			if (entry.th32ProcessID == 0 || entry.th32ProcessID == self)
				continue;
			const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
			if (process == nullptr)
				continue;
			FILETIME created, exited, kernel, user;
			if (GetProcessTimes(process, &created, &exited, &kernel, &user))
				result.push_back({ entry.th32ProcessID, fileTime(created), fileTime(kernel) + fileTime(user),
					QString::fromWCharArray(entry.szExeFile) });
			CloseHandle(process);
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return result;
}

struct Sampler
{
	std::mutex mutex;
	std::condition_variable changed;
	HANDLE guiThread = nullptr;
	DWORD guiId = 0;
	LONGLONG frequency = 0;
	int overrideMs = 0;
	int thresholdMs = 0;
	bool selfTest = false;
	unsigned armedCount = 0;
	bool active = false;
	LONGLONG start = 0;
	Counters startCounters;
	LONGLONG lastWake = 0;
	LONGLONG maxGap = 0;
	LONGLONG lastSample = 0;
	int attempts = 0;
	int samples = 0;
	int stackCount = 0;
	int threadCount = 0;
	std::array<StackSample, maxSamples> stacks;
	std::array<ThreadSnapshot, maxThreads> threads;
	std::array<DWORD64, stackBytes / sizeof(DWORD64)> stackCopy = {};
	std::vector<ProcessTimes> processes;
	ULONGLONG processesTime = 0;

	Sampler()
	{
		LARGE_INTEGER value;
		QueryPerformanceFrequency(&value);
		frequency = value.QuadPart;
		bool ok = false;
		const int configured = qEnvironmentVariableIntValue("EAPO_STALL_SAMPLE_MS", &ok);
		if (ok && configured > 0)
			overrideMs = configured;
		selfTest = qgetenv("EAPO_STALL_SELFTEST") == "1";
		guiId = GetCurrentThreadId();
		if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
			&guiThread, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
			std::thread(&Sampler::run, this).detach();
	}

	double milliseconds(LONGLONG ticks) const
	{
		return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
	}

	void snapshotThreads()
	{
		const DWORD processId = GetCurrentProcessId();
		const DWORD samplerId = GetCurrentThreadId();
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return;
		THREADENTRY32 entry = {};
		entry.dwSize = sizeof(entry);
		if (Thread32First(snapshot, &entry))
		{
			do
			{
				if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != samplerId)
				{
					const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
						FALSE, entry.th32ThreadID);
					if (handle != nullptr)
					{
						ThreadSnapshot& thread = threads[threadCount++];
						thread = {};
						thread.handle = handle;
						thread.id = entry.th32ThreadID;
						thread.startCpu = threadCpuTime(handle);
					}
				}
				entry.dwSize = sizeof(entry);
			} while (threadCount < maxThreads && Thread32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
		// All enumeration, handle creation and CPU baselines precede suspension.
		for (int i = 0; i < threadCount; i++)
			captureStack(threads[i].handle, threads[i].stack, stackCopy.data());
	}

	void run()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
		std::unique_lock<std::mutex> lock(mutex);
		for (;;)
		{
			changed.wait(lock, [this]() { return active; });
			while (active)
			{
				changed.wait_for(lock, std::chrono::milliseconds(25));
				// wait_for releases the mutex; the GUI can disarm us meanwhile.
				// cppcheck-suppress knownConditionTrueFalse
				if (!active)
					break;
				const LONGLONG now = counter();
				maxGap = std::max(maxGap, now - lastWake);
				lastWake = now;
				if (milliseconds(now - start) <= thresholdMs || attempts >= maxSamples
					|| (lastSample != 0 && milliseconds(now - lastSample) < 50.0))
					continue;
				lastSample = now;
				if (attempts == 0)
				{
					processesTime = systemTimeNow();
					processes = readProcessTimes();
					snapshotThreads();
				}
				attempts++;
				StackSample sample;
				captureStack(guiThread, sample, stackCopy.data());
				if (sample.size == 0)
					continue;
				samples++;
				int index = 0;
				for (; index < stackCount; index++)
				{
					if (sameStack(stacks[index], sample))
						break;
				}
				if (index == stackCount)
					stacks[stackCount++] = sample;
				stacks[index].count++;
			}
		}
	}
};

Sampler& sampler()
{
	// The gates use _Exit. Keep both the detached worker's state and its real
	// thread handle alive through process exit; no static destructor joins it.
	static Sampler* const instance = new Sampler;
	return *instance;
}

__declspec(noinline) void selfTestSpin(LONGLONG duration, LONGLONG frequency)
{
	const LONGLONG start = counter();
	while ((counter() - start) * 1000 / frequency < duration) {}
}

bool initializeSymbols()
{
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	return SymInitializeW(GetCurrentProcess(), nullptr, TRUE) != FALSE;
}

void printFrame(DWORD64 address, bool symbolsReady)
{
	const HANDLE process = GetCurrentProcess();
	DWORD64 base = symbolsReady ? SymGetModuleBase64(process, address) : 0;
	if (base == 0)
	{
		// Module+RVA remains useful even if dbghelp could not initialize.
		MEMORY_BASIC_INFORMATION memory = {};
		if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) != 0
			&& memory.Type == MEM_IMAGE)
			base = reinterpret_cast<DWORD64>(memory.AllocationBase);
	}
	static wchar_t path[32768] = {};
	QString module = QStringLiteral("unknown");
	if (base != 0 && GetModuleFileNameW(reinterpret_cast<HMODULE>(base), path, 32768) != 0)
		module = QFileInfo(QString::fromWCharArray(path)).fileName();
	SYMBOL_INFO_PACKAGEW symbol = {};
	symbol.si.SizeOfStruct = sizeof(SYMBOL_INFOW);
	symbol.si.MaxNameLen = MAX_SYM_NAME;
	DWORD64 displacement = 0;
	if (symbolsReady && SymFromAddrW(process, address, &displacement, &symbol.si))
		qWarning("StallWatch:     %s!%s+0x%llx", qPrintable(module),
			qPrintable(QString::fromWCharArray(symbol.si.Name, static_cast<int>(symbol.si.NameLen))),
			static_cast<unsigned long long>(displacement));
	else
		qWarning("StallWatch:     %s+0x%llx", qPrintable(module),
			static_cast<unsigned long long>(address - base));
}

void printThreads(Sampler& state, bool symbolsReady)
{
	if (state.threadCount == 0)
		return;
	std::array<int, maxThreads> ranked = {};
	for (int i = 0; i < state.threadCount; i++)
		ranked[i] = i;
	std::stable_sort(ranked.begin(), ranked.begin() + state.threadCount, [&state](int left, int right) {
		return state.threads[left].cpu > state.threads[right].cpu;
	});
	QStringList times;
	for (int i = 0; i < state.threadCount; i++)
	{
		const ThreadSnapshot& thread = state.threads[ranked[i]];
		times.append(QStringLiteral("%1%2 %3 ms").arg(thread.id)
			.arg(thread.id == state.guiId ? QStringLiteral(" (gui)") : QString())
			.arg(static_cast<double>(thread.cpu) / 10000.0, 0, 'f', 1));
	}
	qWarning("StallWatch:   threads at threshold: %d (cpu since: %s)", state.threadCount, qPrintable(times.join(QStringLiteral(", "))));
	std::array<bool, maxThreads> printed = {};
	int distinct = 0;
	for (int i = 0; i < state.threadCount && distinct < 12; i++)
	{
		if (printed[i])
			continue;
		const StackSample& stack = state.threads[ranked[i]].stack;
		QStringList ids;
		for (int j = i; j < state.threadCount; j++)
		{
			const ThreadSnapshot& thread = state.threads[ranked[j]];
			if (!printed[j] && sameStack(stack, thread.stack))
			{
				printed[j] = true;
				ids.append(QStringLiteral("%1%2").arg(thread.id)
					.arg(thread.id == state.guiId ? QStringLiteral(" (gui)") : QString()));
			}
		}
		qWarning("StallWatch:   thread %s (%lld threads):", qPrintable(ids.join(QStringLiteral(", "))),
			static_cast<long long>(ids.size()));
		if (stack.size == 0)
			qWarning("StallWatch:     stack unavailable");
		for (int frame = 0; frame < std::min(16, stack.size); frame++)
			printFrame(stack.addresses[frame], symbolsReady);
		distinct++;
	}
}

// The machine's other users since the earlier reading: a runner's scanner or
// updater appears here, not among this process's threads.
QString topProcesses(const std::vector<ProcessTimes>& before, ULONGLONG since)
{
	const std::vector<ProcessTimes> after = readProcessTimes();
	std::vector<std::pair<ULONGLONG, const ProcessTimes*>> used;
	for (const ProcessTimes& now : after)
	{
		const auto then = std::find_if(before.cbegin(), before.cend(), [&now](const ProcessTimes& candidate) {
			return candidate.id == now.id && candidate.created == now.created;
		});
		if (then != before.cend())
			used.push_back({ difference(now.cpu, then->cpu), &now });
		else if (now.created >= since)
			used.push_back({ now.cpu, &now });
	}
	std::stable_sort(used.begin(), used.end(), [](const auto& left, const auto& right) {
		return left.first > right.first;
	});
	QStringList top;
	for (size_t i = 0; i < std::min<size_t>(6, used.size()) && used[i].first != 0; i++)
	{
		top.append(QStringLiteral("%1 (%2) %3 ms").arg(used[i].second->name).arg(used[i].second->id)
			.arg(static_cast<double>(used[i].first) / 10000.0, 0, 'f', 1));
	}
	return top.isEmpty() ? QStringLiteral("none measured") : top.join(QStringLiteral(", "));
}
}
#endif

GalleryStallWatch::GalleryStallWatch(const char* gate, const QString& operation, int thresholdMs)
	: gate(gate), operation(operation)
{
#if defined(_M_X64)
	Sampler& state = sampler();
	unsigned sequence = 0;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		if (state.guiThread == nullptr || state.active)
			return;
		state.thresholdMs = state.overrideMs > 0 ? state.overrideMs : thresholdMs;
		state.samples = 0;
		state.attempts = 0;
		state.stackCount = 0;
		state.threadCount = 0;
		state.processes.clear();
		state.processesTime = 0;
		state.maxGap = 0;
		state.lastSample = 0;
		state.startCounters = {};
		state.startCounters.read(state.guiThread);
		state.start = counter();
		state.lastWake = state.start;
		state.active = true;
		armed = true;
		sequence = ++state.armedCount;
	}
	state.changed.notify_one();
	if (state.selfTest && sequence <= 2)
	{
		const LONGLONG duration = static_cast<LONGLONG>(state.thresholdMs) + 600;
		if (sequence == 1)
		{
			std::thread worker(selfTestSpin, duration, state.frequency);
			worker.join();
		}
		else
			selfTestSpin(duration, state.frequency);
	}
#else
	Q_UNUSED(thresholdMs);
#endif
}

GalleryStallWatch::~GalleryStallWatch()
{
#if defined(_M_X64)
	if (!armed)
		return;
	Sampler& state = sampler();
	const LONGLONG end = counter();
	Counters endCounters;
	endCounters.read(state.guiThread);
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.active = false;
		// Include the final partial heartbeat: if the VM stopped and the GUI
		// finished before the sampler ran again, that gap must not disappear.
		state.maxGap = std::max(state.maxGap, end - state.lastWake);
		for (int i = 0; i < state.threadCount; i++)
			state.threads[i].cpu = difference(threadCpuTime(state.threads[i].handle), state.threads[i].startCpu);
	}
	state.changed.notify_one();
	const double wallMs = state.milliseconds(end - state.start);
	if (wallMs > state.thresholdMs)
	{
		const Counters& first = state.startCounters;
		const double busy = systemBusy(first, endCounters);
		qWarning("StallWatch: %s %s: wall %.1f ms, gui cpu %.1f ms, process cpu %.1f ms, system busy %.1f%% of %lu cpus, page faults %lu, reads %llu ops %.1f KiB, writes %llu ops %.1f KiB, working set %.1f MiB, private %.1f MiB, memory load %lu%% (%.1f MiB free), sampler heartbeat max gap %.1f ms, gui samples %d",
			gate, qPrintable(operation), wallMs,
			static_cast<double>(difference(endCounters.guiCpu, first.guiCpu)) / 10000.0,
			static_cast<double>(difference(endCounters.processCpu, first.processCpu)) / 10000.0,
			busy, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
			endCounters.memory.PageFaultCount - first.memory.PageFaultCount,
			difference(endCounters.io.ReadOperationCount, first.io.ReadOperationCount),
			static_cast<double>(difference(endCounters.io.ReadTransferCount, first.io.ReadTransferCount)) / 1024.0,
			difference(endCounters.io.WriteOperationCount, first.io.WriteOperationCount),
			static_cast<double>(difference(endCounters.io.WriteTransferCount, first.io.WriteTransferCount)) / 1024.0,
			static_cast<double>(endCounters.memory.WorkingSetSize) / (1024.0 * 1024.0),
			static_cast<double>(endCounters.memory.PrivateUsage) / (1024.0 * 1024.0),
			endCounters.globalMemory.dwMemoryLoad, static_cast<double>(endCounters.globalMemory.ullAvailPhys) / (1024.0 * 1024.0),
			state.milliseconds(state.maxGap), state.samples);
		// The sampler is disarmed and never calls dbghelp. Symbol loading and
		// formatting happen only here, after the gate recorded its elapsed time.
		static const bool symbolsReady = initializeSymbols();
		std::array<int, maxSamples> ranked = {};
		for (int i = 0; i < state.stackCount; i++)
			ranked[i] = i;
		std::stable_sort(ranked.begin(), ranked.begin() + state.stackCount, [&state](int left, int right) {
			return state.stacks[left].count > state.stacks[right].count;
		});
		for (int i = 0; i < std::min(3, state.stackCount); i++)
		{
			const StackSample& stack = state.stacks[ranked[i]];
			qWarning("StallWatch:   [%d/%d]", stack.count, state.samples);
			for (int frame = 0; frame < std::min(24, stack.size); frame++)
				printFrame(stack.addresses[frame], symbolsReady);
		}
		printThreads(state, symbolsReady);
		if (state.processesTime != 0 && !state.processes.empty())
			qWarning("StallWatch:   other processes, cpu since threshold: %s",
				qPrintable(topProcesses(state.processes, state.processesTime)));
	}
	for (int i = 0; i < state.threadCount; i++)
	{
		CloseHandle(state.threads[i].handle);
		state.threads[i].handle = nullptr;
	}
#endif
}

#if defined(_M_X64)
struct GalleryLoadSummary::State
{
	LONGLONG start = 0;
	LONGLONG frequency = 0;
	Counters counters;
	ULONGLONG processesTime = 0;
	std::vector<ProcessTimes> processes;
};
#else
struct GalleryLoadSummary::State
{
};
#endif

GalleryLoadSummary::GalleryLoadSummary(const char* gate)
	: gate(gate), state(std::make_unique<State>())
{
#if defined(_M_X64)
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	state->frequency = frequency.QuadPart;
	state->counters.read(GetCurrentThread());
	state->processesTime = systemTimeNow();
	state->processes = readProcessTimes();
	state->start = counter();
#endif
}

GalleryLoadSummary::~GalleryLoadSummary() = default;

void GalleryLoadSummary::report() const
{
#if defined(_M_X64)
	const LONGLONG end = counter();
	Counters last;
	last.read(GetCurrentThread());
	const Counters& first = state->counters;
	qWarning("StallWatch: %s whole run: wall %.1f s, gui cpu %.1f s, process cpu %.1f s, system busy %.1f%% of %lu cpus; other processes: %s",
		gate, static_cast<double>(end - state->start) / static_cast<double>(state->frequency),
		static_cast<double>(difference(last.guiCpu, first.guiCpu)) / 1.0e7,
		static_cast<double>(difference(last.processCpu, first.processCpu)) / 1.0e7,
		systemBusy(first, last), GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
		qPrintable(topProcesses(state->processes, state->processesTime)));
#endif
}
