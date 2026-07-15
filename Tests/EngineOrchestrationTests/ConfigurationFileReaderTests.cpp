/*
	This file is part of EqualizerAPO-XT.

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

#include "ConfigurationFileReader.h"
#include "Tests/TestHarness.h"

void runConfigurationFileReaderTests(test::Harness& harness)
{
	wchar_t tempPath[MAX_PATH] = {};
	DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
	harness.require(tempLength > 0 && tempLength < MAX_PATH, "open-failure test obtains the temporary directory");
	const std::wstring missingPath = std::wstring(tempPath) + L"EapoMissingConfig-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
	DeleteFileW(missingPath.c_str());
	std::stringstream missing = ConfigurationFileReader::readWithRetry(missingPath);
	harness.expectFalse(missing.good(), "readWithRetry reports an open failure");

	const std::wstring pipeName = L"\\\\.\\pipe\\EngineOrchestrationTests-ConfigRead-" + std::to_wstring(GetCurrentProcessId());
	HANDLE pipe = CreateNamedPipeW(
		pipeName.c_str(),
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		nullptr);
	harness.require(pipe != INVALID_HANDLE_VALUE, "partial configuration read creates a named pipe");

	bool serverSucceeded = false;
	std::thread server([&]() {
		BOOL connected = ConnectNamedPipe(pipe, nullptr);
		if (!connected && GetLastError() == ERROR_PIPE_CONNECTED)
			connected = TRUE;
		const char prefix[] = "Preamp: -6 dB\r\n";
		DWORD written = 0;
		if (connected && WriteFile(pipe, prefix, sizeof(prefix) - 1, &written, nullptr) && written == sizeof(prefix) - 1)
			serverSucceeded = FlushFileBuffers(pipe) != FALSE;
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
	});

	std::stringstream input = ConfigurationFileReader::readWithRetry(pipeName);
	server.join();
	harness.require(serverSucceeded, "partial configuration read sends the prefix");
	harness.expectFalse(input.good(), "readWithRetry reports a ReadFile failure");
	harness.expectTrue(input.str().empty(), "readWithRetry does not expose a partial configuration");
}
