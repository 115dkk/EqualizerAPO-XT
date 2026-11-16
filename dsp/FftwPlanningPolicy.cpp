/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fftw3.h>

#include "dsp/FftwPlanningPolicy.h"
#include "services/logging/Logging.h"

namespace
{
std::mutex& plannerMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::wstring& wisdomPath()
{
	static std::wstring path;
	return path;
}

std::unordered_set<int>& exportedLengths()
{
	static std::unordered_set<int> lengths;
	return lengths;
}

void logWisdomFailureOnce(const wchar_t* operation, const std::wstring& path)
{
	static std::atomic_flag logged = ATOMIC_FLAG_INIT;
	if (logged.test_and_set())
		return;
	if (path.empty())
		LogFStatic(L"Could not %s FFTW wisdom because LOCALAPPDATA is unavailable", operation);
	else
		LogFStatic(L"Could not %s FFTW wisdom file %s", operation, path.c_str());
}

std::wstring localAppData()
{
	const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	if (required == 0)
		return L"";
	std::vector<wchar_t> buffer(required);
	const DWORD written = GetEnvironmentVariableW(
		L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
	if (written == 0 || written >= static_cast<DWORD>(buffer.size()))
		return L"";
	return std::wstring(buffer.data(), written);
}

void initializeWisdom()
{
	static std::once_flag initialized;
	std::call_once(initialized, [] {
		const std::wstring appData = localAppData();
		if (appData.empty())
		{
			logWisdomFailureOnce(L"locate", L"");
			return;
		}

		const std::wstring directory = appData + L"\\EqualizerAPO";
		if (!CreateDirectoryW(directory.c_str(), nullptr)
			&& GetLastError() != ERROR_ALREADY_EXISTS)
		{
			logWisdomFailureOnce(L"create the directory for", directory);
			return;
		}
		wisdomPath() = directory + L"\\fftw_wisdom.dat";
		if (GetFileAttributesW(wisdomPath().c_str()) == INVALID_FILE_ATTRIBUTES)
			return;

		FILE* input = nullptr;
		if (_wfopen_s(&input, wisdomPath().c_str(), L"rb") != 0 || input == nullptr)
		{
			logWisdomFailureOnce(L"open", wisdomPath());
			return;
		}
		const bool imported = fftw_import_wisdom_from_file(input) != 0;
		const bool closed = fclose(input) == 0;
		if (!imported || !closed)
			logWisdomFailureOnce(L"import", wisdomPath());
	});
}
}

void FftwPlanningPolicy::ensurePlannerThreadSafe()
{
	static std::once_flag installed;
	std::call_once(installed, [] {
		fftw_make_planner_thread_safe();
	});
}

FftwPlanningPolicy::Session::Session()
	: plannerLock((ensurePlannerThreadSafe(), plannerMutex()))
{
	initializeWisdom();
}

// Deliberately requires a live Session: the Session owns the planner lock.
unsigned FftwPlanningPolicy::Session::flags() const
{
	// The first plan for a transform length measures and becomes reusable
	// wisdom. Imported wisdom makes the same flag fast on later processes.
	return FFTW_MEASURE | FFTW_PRESERVE_INPUT;
}

// Deliberately requires a live Session: FFTW wisdom mutation is serialized by
// the planner lock held for this object's lifetime.
bool FftwPlanningPolicy::Session::exportWisdomForLength(int transformLength)
{
	if (wisdomPath().empty())
		return false;
	if (exportedLengths().contains(transformLength))
		return true;

	FILE* output = nullptr;
	if (_wfopen_s(&output, wisdomPath().c_str(), L"wb") != 0 || output == nullptr)
	{
		logWisdomFailureOnce(L"open for export", wisdomPath());
		return false;
	}
	fftw_export_wisdom_to_file(output);
	const bool wrote = ferror(output) == 0;
	const bool closed = fclose(output) == 0;
	if (!wrote || !closed)
	{
		logWisdomFailureOnce(L"export", wisdomPath());
		return false;
	}
	exportedLengths().insert(transformLength);
	return true;
}
