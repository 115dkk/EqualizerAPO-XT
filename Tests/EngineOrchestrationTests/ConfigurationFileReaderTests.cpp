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

#include "engine/ConfigurationFileReader.h"
#include "Tests/TestHarness.h"
#include "platform/windows/Win32Resource.h"

void runConfigurationFileReaderTests(test::Harness& harness)
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

	wchar_t tempPath[MAX_PATH] = {};
	DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
	harness.require(tempLength > 0 && tempLength < MAX_PATH, "open-failure test obtains the temporary directory");
	const std::wstring missingPath = std::wstring(tempPath) + L"EapoMissingConfig-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
	DeleteFileW(missingPath.c_str());
	std::stringstream missing = ConfigurationFileReader::readWithRetry(missingPath);
	harness.expectFalse(missing.good(), "readWithRetry reports an open failure");

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
