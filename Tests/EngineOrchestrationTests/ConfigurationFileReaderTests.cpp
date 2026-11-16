/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ConfigurationFileReader failure-path tests. A named pipe provides real
	ReadFile behavior: the first read succeeds, then the server disconnects so
	the next read fails with ERROR_BROKEN_PIPE.
*/

#include <sstream>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/ConfigurationFileReader.h"
#include "../TestHarness.h"
#include "platform/windows/FileSharingRetry.h"
#include "platform/windows/Win32Resource.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
class CountingRetryPolicy final : public FileSharingRetryPolicy
{
public:
	ULONGLONG now() const override
	{
		return elapsedMilliseconds;
	}

	bool wait(HANDLE cancel, DWORD milliseconds) const override
	{
		++waitCount;
		if (cancel != nullptr && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0)
		{
			cancellationObserved = true;
			return true;
		}
		elapsedMilliseconds += milliseconds;
		return false;
	}

	mutable ULONGLONG elapsedMilliseconds = 0;
	mutable unsigned waitCount = 0;
	mutable bool cancellationObserved = false;
};
}

TEST_CASE(EngineOrchestrationTests, runConfigurationFileReaderTests)
{
	const std::vector<std::wstring> mixed = ConfigurationFileReader::decodeLines(
		std::string("Preamp: -6 dB\r\nInclude: a.txt\nlast"));
	harness.requireEqual(static_cast<int>(mixed.size()), 3,
		"decodeLines splits CRLF and LF terminated lines");
	harness.expect(mixed[0] == L"Preamp: -6 dB",
		"decodeLines strips a trailing carriage return");
	harness.expect(mixed[2] == L"last",
		"decodeLines preserves a final unterminated line");

	const std::vector<std::wstring> unicode = ConfigurationFileReader::decodeLines(
		std::string("# caf\xC3\xA9"));
	harness.requireEqual(static_cast<int>(unicode.size()), 1,
		"decodeLines returns one UTF-8 line");
	harness.expect(unicode[0] == L"# caf\u00e9",
		"decodeLines decodes valid UTF-8");

	const std::vector<std::wstring> marked = ConfigurationFileReader::decodeLines(
		std::string("\xEF\xBB\xBFPreamp: -6 dB\r\nInclude: a.txt"));
	harness.requireEqual(static_cast<int>(marked.size()), 2,
		"decodeLines splits a file that starts with a byte order mark");
	harness.expect(marked[0] == L"Preamp: -6 dB",
		"decodeLines drops the byte order mark that starts the file");
	harness.expect(marked[1] == L"Include: a.txt",
		"decodeLines leaves the following lines as they are");

	harness.require(GetFileAttributesW(testDirectory().c_str()) != INVALID_FILE_ATTRIBUTES,
		"open-failure test has its temporary directory");
	const std::wstring missingPath = testDirectory() + L"\\MissingConfig.txt";
	DeleteFileW(missingPath.c_str());
	std::stringstream missing = ConfigurationFileReader::readWithRetry(missingPath);
	harness.expectFalse(missing.good(), "readWithRetry reports an open failure");

	const std::wstring lockedPath = testDirectory() + L"\\LockedConfig.txt";
	winutil::UniqueHandle lockedFile(CreateFileW(
		lockedPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	harness.require(static_cast<bool>(lockedFile), "sharing-retry test creates an exclusively shared file");
	winutil::UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, TRUE, nullptr));
	harness.require(static_cast<bool>(cancelEvent), "sharing-retry test creates a pre-signaled cancel event");

	CountingRetryPolicy cancelPolicy;
	std::stringstream cancelled = ConfigurationFileReader::readWithRetry(
		lockedPath, cancelEvent.get(), 10000, cancelPolicy);
	harness.expectFalse(cancelled.good(), "readWithRetry cancels a sharing retry");
	harness.expectEqual(cancelPolicy.waitCount, 1u,
		"readWithRetry asks the wait policy once before observing cancellation");
	harness.expectTrue(cancelPolicy.cancellationObserved,
		"readWithRetry gives the pre-signaled cancel handle to the wait policy");
	harness.expectEqual(cancelPolicy.elapsedMilliseconds, ULONGLONG(0),
		"readWithRetry cancellation consumes no fake-clock time");

	CountingRetryPolicy deadlinePolicy;
	std::stringstream timedOut = ConfigurationFileReader::readWithRetry(
		lockedPath, nullptr, 100, deadlinePolicy);
	harness.expectFalse(timedOut.good(), "readWithRetry times out a sharing retry");
	harness.expectEqual(deadlinePolicy.elapsedMilliseconds, ULONGLONG(100),
		"readWithRetry stops at the injected deadline");
	harness.expectEqual(deadlinePolicy.waitCount, 9u,
		"readWithRetry follows the bounded exponential retry schedule");
	lockedFile.reset();
	DeleteFileW(lockedPath.c_str());

	const std::wstring pipeName = L"\\\\.\\pipe\\EngineOrchestrationTests-ConfigRead-" + std::to_wstring(GetCurrentProcessId());
	winutil::UniqueHandle pipe(CreateNamedPipeW(
		pipeName.c_str(),
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		nullptr));
	harness.require(static_cast<bool>(pipe), "partial configuration read creates a named pipe");

	bool serverSucceeded = false;
	std::thread server([&]() {
		BOOL connected = ConnectNamedPipe(pipe.get(), nullptr);
		if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
			connected = TRUE;
		const char prefix[] = "Preamp: -6 dB\r\n";
		DWORD written = 0;
		if (connected && WriteFile(pipe.get(), prefix, sizeof(prefix) - 1, &written, nullptr) && written == sizeof(prefix) - 1)
			serverSucceeded = FlushFileBuffers(pipe.get()) != FALSE;
		DisconnectNamedPipe(pipe.get());
	});

	std::stringstream input = ConfigurationFileReader::readWithRetry(pipeName);
	server.join();
	harness.require(serverSucceeded, "partial configuration read sends the prefix");
	harness.expectFalse(input.good(), "readWithRetry reports a ReadFile failure");
	harness.expectTrue(input.str().empty(), "readWithRetry does not expose a partial configuration");
}
