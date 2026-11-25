/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2012  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "services/registry/RegistryPaths.h"
#include <cerrno>
#include <cstdarg>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/registry/WindowsRegistry.h"
#include "services/logging/Logging.h"
#include "platform/windows/WindowsPath.h"

using std::log;
using std::wstring;

std::atomic<bool> Logging::initialized{false};
wstring Logging::logPath;
std::atomic<bool> Logging::enableTrace{false};
FILE* Logging::presetFP = nullptr;
bool Logging::compact = false;
bool Logging::useConsoleColors = false;

namespace
{
std::mutex& logStateMutex()
{
	static std::mutex mutex;
	return mutex;
}

// One line at a time within the process (#527). Each line opens the file
// unshared, which keeps it whole against another process appending to the same
// log, but an open made while anyone else holds the file fails. Two engines in
// the audio service log at the same millisecond when a configuration reloads,
// and the one that lost the race used to drop its line.
std::mutex& logWriteMutex()
{
	static std::mutex mutex;
	return mutex;
}

// Another process can still hold the file for the length of its own line, so
// a sharing violation is waited out briefly instead of dropping the line. The
// bound keeps a file that someone holds open for long from stalling the
// thread that logs; any other failure returns at once.
errno_t openLogForAppend(FILE** fp, const std::wstring& path)
{
	const ULONGLONG deadline = GetTickCount64() + 100;
	for (;;)
	{
		const errno_t err = _wfopen_s(fp, path.c_str(), L"at, ccs=UTF-8");
		unsigned long osError = 0;
		if (err == 0 || _get_doserrno(&osError) != 0 || osError != ERROR_SHARING_VIOLATION
			|| GetTickCount64() >= deadline)
			return err;
		Sleep(1);
	}
}

DWORD rotateLargeGuiLog(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA data{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
	{
		const DWORD error = GetLastError();
		return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ERROR_SUCCESS : error;
	}
	if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
		return ERROR_SUCCESS;
	const ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	if (size <= 4ULL * 1024 * 1024)
		return ERROR_SUCCESS;
	// One rename preserves complete diagnostics without reading a potentially
	// huge trace file. Only GUI startup calls this (audit #467 TD-60).
	return MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING)
		? ERROR_SUCCESS : GetLastError();
}
}

void Logging::log(const char* file, int line, const void* caller, bool trace, const wchar_t* format, ...)
{
	// Two concurrent first-loggers would race on logPath (a std::wstring).
	// Double-checked init under a mutex; the release store pairs with the
	// acquire load so later loggers see logPath.
	if (!initialized.load(std::memory_order_acquire))
	{
		std::lock_guard<std::mutex> lock(logStateMutex());
		if (!initialized.load(std::memory_order_relaxed))
		{
			wchar_t temp[255];
			// Audit #250 F042: an unchecked failure used to leave `temp`
			// uninitialized and the log path garbage - and that failure was
			// itself unobservable. With no temp path, run without a file.
			if (GetTempPathW(sizeof(temp) / sizeof(wchar_t), temp) == 0)
				temp[0] = L'\0';

			logPath = temp;
			if (!logPath.empty())
				logPath += L"EqualizerAPO.log";

			// Publish before the registry probe, and do not try to initialize
			// again even in case of error: the catch below logs through
			// LogFStatic, which re-enters log() on this thread and must take
			// the fast path (logPath is set) instead of deadlocking here.
			initialized.store(true, std::memory_order_release);

			try
			{
				if (systemRegistry().readValue(APP_REGPATH, enableTraceValueName) != L"false")
					enableTrace = true;
			}
			catch (const RegistryError& e)
			{
				// getMessage() returns a std::wstring; passing it through
				// varargs is undefined behavior, hence .c_str().
				LogFStatic(L"%s", e.getMessage().c_str());
			}
		}
	}

	if (trace && !enableTrace)
		return;

	// Held to the end of the line: a stream destination shared between threads
	// would otherwise interleave one line's prefix, text and newline with
	// another's.
	std::lock_guard<std::mutex> writeLock(logWriteMutex());
	FILE* fp;
	if (presetFP == nullptr)
	{
		// Preserve pre-existing ANSI bytes rather than guessing their code page
		// or rotating from audiodg. New entries use UTF-8; new files get a BOM
		// (testLogKeepsUnicodeAndWin32ErrorNumbers, audit #467 TD-12).
		errno_t err = openLogForAppend(&fp, logPath);
		if (err != 0)
			return;
	}
	else
	{
		fp = presetFP;
	}

	if (useConsoleColors)
	{
		HANDLE con = GetStdHandle(STD_OUTPUT_HANDLE);
		if (trace)
			SetConsoleTextAttribute(con, 2);// Set console color to green
		else
			SetConsoleTextAttribute(con, 12);// Set console color to red
	}

	if (!compact)
	{
		SYSTEMTIME ___st;
		GetLocalTime(&___st);
		DWORD threadId = GetCurrentThreadId();
		fwprintf(fp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d %d %08X (%S:%d): ",
			___st.wYear, ___st.wMonth, ___st.wDay, ___st.wHour, ___st.wMinute, ___st.wSecond, ___st.wMilliseconds, threadId, static_cast<DWORD>(reinterpret_cast<uintptr_t>(caller)), file, line);
	}

	if (trace)
		fwprintf(fp, L"(TRACE) ");

	va_list varArgs;
	va_start(varArgs, format);
	vfwprintf(fp, format, varArgs);
	va_end(varArgs);

	fwprintf(fp, L"\n");

	if (useConsoleColors)
	{
		HANDLE con = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(con, 7); // Set console color to light grey (default)
	}

	if (presetFP == nullptr)
		fclose(fp);
	else
		fflush(fp);
}

void Logging::refreshTrace()
{
	// A missing value means off, as it does at first use; reading it is the
	// only thing that can fail, and that is worth a line in the log.
	bool trace = false;
	try
	{
		if (systemRegistry().valueExists(APP_REGPATH, enableTraceValueName))
			trace = systemRegistry().readValue(APP_REGPATH, enableTraceValueName) != L"false";
	}
	catch (const RegistryError& e)
	{
		LogFStatic(L"%s", e.getMessage().c_str());
	}
	enableTrace.store(trace);
}

void Logging::set(FILE* fp, bool enableTrace, bool compact, bool useConsoleColors)
{
	useStream(fp, enableTrace, compact, useConsoleColors);
}

std::wstring Logging::currentPath()
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	// presetFP wins in log(), so a stream destination has no path to report.
	return presetFP == nullptr ? logPath : std::wstring();
}

void Logging::useDefaultApoLog()
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = nullptr;
	logPath.clear();
	enableTrace.store(false);
	compact = false;
	useConsoleColors = false;
	initialized.store(false, std::memory_order_release);
}

void Logging::useFile(const std::wstring& path, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = nullptr;
	logPath = path;
	enableTrace.store(traceEnabled);
	compact = compactOutput;
	useConsoleColors = consoleColors;
	initialized.store(true, std::memory_order_release);
}

std::wstring Logging::userLogDirectory()
{
	const auto local = pathutil::environmentVariable(L"LOCALAPPDATA");
	return local.empty() ? std::wstring() : pathutil::joinPath(local, L"EqualizerAPO\\logs");
}

bool Logging::useUserFile(const std::wstring& fileName, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	const auto directory = userLogDirectory();
	if (directory.empty() || fileName.empty() || !pathutil::createDirectoryRecursive(directory))
		return false;
	const auto path = pathutil::joinPath(directory, fileName);
	// Do not rotate APO/ASIO logs from an audio process or from log().
	const DWORD rotationError = fileName == L"Editor.log" || fileName == L"DeviceSelector.log"
		? rotateLargeGuiLog(path) : ERROR_SUCCESS;
	useFile(path, traceEnabled, compactOutput, consoleColors);
	if (rotationError != ERROR_SUCCESS)
		LogFStatic(L"Could not rotate the startup log (error %lu); keeping the existing file", rotationError);
	return true;
}

void Logging::useStream(FILE* fp, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = fp;
	logPath.clear();
	enableTrace.store(traceEnabled);
	compact = compactOutput;
	useConsoleColors = consoleColors;
	initialized.store(true, std::memory_order_release);
}
