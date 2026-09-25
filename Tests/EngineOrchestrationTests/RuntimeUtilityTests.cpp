/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The small runtime pieces the engine and the APO lean on, each pinned by
	itself: the COM boundary's exception mapping, the log destinations, the
	registry export header, SynchronizedState, ParallelExecutor and
	WeakValueCache.
*/

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/ComBoundary.h"
#include "runtime/WeakValueCache.h"
#include "runtime/concurrency/ParallelExecutor.h"
#include "runtime/concurrency/SynchronizedState.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"

#include "EngineOrchestrationTestSupport.h"

// Allocation failure and any other exception become HRESULTs at the COM
// boundary instead of unwinding into audiodg.exe.
void testComBoundaryMapsExceptions(test::Harness& harness)
{
	try
	{
		harness.expect(ComBoundary::invoke([]() -> HRESULT {
			throw std::bad_alloc();
		}) == E_OUTOFMEMORY, "COM boundary maps allocation failure");
	}
	catch (...)
	{
		harness.fail("allocation exception escaped the COM boundary");
	}
	try
	{
		harness.expect(ComBoundary::invoke([]() -> HRESULT {
			throw std::runtime_error("injected COM failure");
		}) == E_UNEXPECTED, "COM boundary maps unexpected exception");
	}
	catch (...)
	{
		harness.fail("unexpected exception escaped the COM boundary");
	}
	harness.expect(ComBoundary::invoke([] { return S_FALSE; }) == S_FALSE,
		"COM boundary preserves callback HRESULT");
}

void testLogHelperFileDestination(test::Harness& harness)
{
	const std::wstring path = testDirectory() + L"\\LogHelperDestination.log";
	DeleteFileW(path.c_str());

	Logging::useFile(path, true, false, false);
	LogFStatic(L"file destination %d", 42);

	std::ifstream stream(path, std::ios::binary);
	const std::string contents((std::istreambuf_iterator<char>(stream)),
		std::istreambuf_iterator<char>());
	harness.expect(stream.good() || stream.eof(), "file logger writes a readable destination");
	harness.expect(contents.find("file destination 42") != std::string::npos,
		"file logger writes diagnostics to the selected path");

	Logging::useStream(stderr, false, false, false);
	DeleteFileW(path.c_str());
}

void testLogHelperUserDestination(test::Harness& harness)
{
	wchar_t previousLocalAppData[MAX_PATH] = {};
	const DWORD previousLength = GetEnvironmentVariableW(
		L"LOCALAPPDATA", previousLocalAppData, MAX_PATH);
	const std::wstring localRoot = testDirectory() + L"\\LocalAppData";
	CreateDirectoryW(localRoot.c_str(), nullptr);
	SetEnvironmentVariableW(L"LOCALAPPDATA", localRoot.c_str());

	harness.expect(Logging::useUserFile(L"Editor.log", true, false, false),
		"user logger creates its product log directory");
	LogFStatic(L"user destination");
	const std::wstring path = localRoot + L"\\EqualizerAPO\\logs\\Editor.log";
	std::ifstream stream(path, std::ios::binary);
	const std::string contents((std::istreambuf_iterator<char>(stream)),
		std::istreambuf_iterator<char>());
	harness.expect(contents.find("user destination") != std::string::npos,
		"user logger writes to the per-user Editor log");

	Logging::useStream(stderr, false, false, false);
	if (previousLength > 0 && previousLength < MAX_PATH)
		SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData);
	else
		SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
	DeleteFileW(path.c_str());
	RemoveDirectoryW((localRoot + L"\\EqualizerAPO\\logs").c_str());
	RemoveDirectoryW((localRoot + L"\\EqualizerAPO").c_str());
	RemoveDirectoryW(localRoot.c_str());
}

void testRegistryExportHeaderPreservesQualifiedRoot(test::Harness& harness)
{
	const std::wstring key = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor\\Device\\FxProperties";
	harness.expect(WindowsRegistry::formatExportHeader(key)
			== L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor\\Device\\FxProperties]",
		"registry export writes an already-qualified key exactly once");
}

void testSynchronizedStateSerializesReplacement(test::Harness& harness)
{
	SynchronizedState<int> state(1);
	std::promise<void> enteredPromise;
	std::future<void> entered = enteredPromise.get_future();
	std::promise<void> releasePromise;
	std::shared_future<void> release = releasePromise.get_future().share();

	std::future<int> reader = std::async(std::launch::async, [&]() {
		return state.withLock([&](const int& value) {
			enteredPromise.set_value();
			release.wait();
			return value;
		});
	});
	harness.expect(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		"synchronized state reader acquires the state");

	std::future<void> replacement = std::async(std::launch::async, [&]() {
		state.replace(2);
	});
	harness.expect(replacement.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
		"state replacement waits for an active reader");
	releasePromise.set_value();
	reader.wait();
	replacement.wait();
	harness.expectEqual(reader.get(), 1, "active reader keeps the original state alive");
	replacement.get();
	harness.expectEqual(state.withLock([](const int& value) { return value; }), 2,
		"subsequent reader observes the complete replacement");
}

void testParallelExecutor(test::Harness& harness)
{
	constexpr size_t taskCount = 257;
	std::vector<std::atomic<unsigned>> visits(taskCount);
	for (std::atomic<unsigned>& visit : visits)
		visit.store(0, std::memory_order_relaxed);

	ParallelExecutor::forEach(taskCount, [&](size_t index) {
		visits[index].fetch_add(1, std::memory_order_relaxed);
	}, 4);
	for (size_t index = 0; index < taskCount; ++index)
		harness.expectEqual(visits[index].load(std::memory_order_relaxed), 1u,
			"parallel executor visits each task exactly once");

	bool propagated = false;
	try
	{
		ParallelExecutor::forEach(64, [](size_t index) {
			if (index == 7)
				throw std::runtime_error("parallel operation failed");
		}, 4);
	}
	catch (const std::runtime_error& error)
	{
		propagated = std::string(error.what()) == "parallel operation failed";
	}
	harness.expect(propagated, "parallel executor joins workers and propagates the first exception");
}

// Audit #275 A5/TD-32: the weak-value cache dynamics (hit while alive, miss
// after the last user drops its reference, expired-slot pruning on store)
// used to be hand-rolled twice, in IrCache.cpp and GraphicEQFilter.cpp, and
// tested nowhere; one utility, one test, both consumers covered.
void testWeakValueCacheKeepsEntriesExactlyAsLongAsSomeoneUsesThem(test::Harness& harness)
{
	WeakValueCache<int, const std::vector<double>> cache;

	auto first = std::make_shared<const std::vector<double>>(3, 1.0);
	cache.store(1, first);
	harness.expect(cache.find(1) == first, "a stored entry is found while a user holds it");
	harness.expect(cache.find(2) == nullptr, "an unknown key misses");

	{
		auto second = std::make_shared<const std::vector<double>>(3, 2.0);
		cache.store(2, second);
		harness.expect(cache.find(2) == second, "a second entry lives alongside the first");
		harness.expectEqual(cache.liveCount(), size_t(2), "both entries are live while both are held");
	}
	harness.expect(cache.find(2) == nullptr,
		"an entry dies with its last shared_ptr - the cache holds only weak references");

	// Pruning: storing anything sweeps the expired slot, so the map cannot
	// accumulate dead keys across config reloads.
	auto third = std::make_shared<const std::vector<double>>(3, 3.0);
	cache.store(3, third);
	harness.expectEqual(cache.liveCount(), size_t(2), "the expired slot was pruned on store");

	// store-or-replace: the last writer's live entry wins.
	auto replacement = std::make_shared<const std::vector<double>>(3, 4.0);
	cache.store(1, replacement);
	harness.expect(cache.find(1) == replacement, "storing an existing key replaces its entry");
}
