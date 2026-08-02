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
#include <cstdarg>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "RegistryHelper.h"
#include "LogHelper.h"

using std::log;
using std::wstring;

std::atomic<bool> LogHelper::initialized{false};
wstring LogHelper::logPath;
std::atomic<bool> LogHelper::enableTrace{false};
FILE* LogHelper::presetFP = nullptr;
bool LogHelper::compact = false;
bool LogHelper::useConsoleColors = false;

namespace
{
std::mutex& logStateMutex()
{
	static std::mutex mutex;
	return mutex;
}
}

void LogHelper::log(const char* file, int line, const void* caller, bool trace, const wchar_t* format, ...)
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
				if (RegistryHelper::readValue(APP_REGPATH, L"EnableTrace") != L"false")
					enableTrace = true;
			}
			catch (const RegistryException& e)
			{
				// getMessage() returns a std::wstring; passing it through
				// varargs is undefined behavior, hence .c_str().
				LogFStatic(L"%s", e.getMessage().c_str());
			}
		}
	}

	if (trace && !enableTrace)
		return;

	FILE* fp;
	if (presetFP == nullptr)
	{
		errno_t err = _wfopen_s(&fp, logPath.c_str(), L"at");
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

void LogHelper::reset()
{
	useDefaultApoLog();
}

void LogHelper::set(FILE* fp, bool enableTrace, bool compact, bool useConsoleColors)
{
	useStream(fp, enableTrace, compact, useConsoleColors);
}

std::wstring LogHelper::currentPath()
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	// presetFP wins in log(), so a stream destination has no path to report.
	return presetFP == nullptr ? logPath : std::wstring();
}

void LogHelper::useDefaultApoLog()
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = nullptr;
	logPath.clear();
	enableTrace.store(false);
	compact = false;
	useConsoleColors = false;
	initialized.store(false, std::memory_order_release);
}

void LogHelper::useFile(const std::wstring& path, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = nullptr;
	logPath = path;
	enableTrace.store(traceEnabled);
	compact = compactOutput;
	useConsoleColors = consoleColors;
	initialized.store(true, std::memory_order_release);
}

bool LogHelper::useUserFile(const std::wstring& fileName, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	wchar_t localAppData[MAX_PATH] = {};
	DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
	if (length == 0 || length >= MAX_PATH || fileName.empty())
		return false;

	std::wstring productDirectory = std::wstring(localAppData, length) + L"\\EqualizerAPO";
	if (!CreateDirectoryW(productDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;

	std::wstring logDirectory = productDirectory + L"\\logs";
	if (!CreateDirectoryW(logDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
		return false;

	useFile(logDirectory + L"\\" + fileName, traceEnabled, compactOutput, consoleColors);
	return true;
}

void LogHelper::useStream(FILE* fp, bool traceEnabled, bool compactOutput, bool consoleColors)
{
	std::lock_guard<std::mutex> lock(logStateMutex());
	presetFP = fp;
	logPath.clear();
	enableTrace.store(traceEnabled);
	compact = compactOutput;
	useConsoleColors = consoleColors;
	initialized.store(true, std::memory_order_release);
}
