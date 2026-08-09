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

#pragma once

#include <atomic>
#include <string>
#include <cstdio>

#define TraceF(format, ...) Logging::log(__FILE__, __LINE__, this, true, format, ##__VA_ARGS__)
#define TraceFStatic(format, ...) Logging::log(__FILE__, __LINE__, nullptr, true, format, ##__VA_ARGS__)
#define LogF(format, ...) Logging::log(__FILE__, __LINE__, this, false, format, ##__VA_ARGS__)
#define LogFStatic(format, ...) Logging::log(__FILE__, __LINE__, nullptr, false, format, ##__VA_ARGS__)

class Logging
{
public:
	static void log(const char* file, int line, const void* caller, bool trace, const wchar_t* format, ...);
	static void useDefaultApoLog();
	static void useFile(const std::wstring& path, bool enableTrace, bool compact, bool useConsoleColors);
	static bool useUserFile(const std::wstring& fileName, bool enableTrace, bool compact, bool useConsoleColors);
	static void useStream(FILE* fp, bool enableTrace, bool compact, bool useConsoleColors);
	static void reset();
	static void set(FILE* fp, bool enableTrace, bool compact, bool useConsoleColors);

	// Where log() is writing, or an empty string when it is writing to a stream
	// (the test suites) or has not been initialised yet. Exists so a program can
	// tell the user where to look: an error dialog that says what went wrong but
	// not where the detail is leaves the user with nothing to send.
	static std::wstring currentPath();

private:
	// First log() call may race between RT, worker, and GUI threads: the
	// acquire load on `initialized` publishes `logPath` written under the init
	// mutex in log(). reset()/set() stay single-threaded test/tool helpers.
	static std::atomic<bool> initialized;
	static std::wstring logPath;
	static std::atomic<bool> enableTrace;
	static FILE* presetFP;
	static bool compact;
	static bool useConsoleColors;
};
