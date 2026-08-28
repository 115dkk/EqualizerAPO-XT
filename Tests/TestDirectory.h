/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Per-process temporary test directory with tracked cleanup - the safest of
	the seven hand-rolled temp-directory fixtures the suites used to carry
	(audit #275 D5/TD-23; the pattern is EngineOrchestrationTests', which was
	the only copy with a PID suffix and a cleanup list). Header-only and
	framework-free like TestHarness.h. Use this for new suites; the older
	per-file GetTempFileNameW fixtures keep working but should migrate here
	when touched.
*/

#pragma once

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace test
{

// One directory per (suite, process): parallel runs on one machine cannot
// collide, and removeAll() deletes exactly the files this object created.
class TestDirectory
{
public:
	explicit TestDirectory(const std::wstring& suiteName)
	{
		wchar_t tempPath[MAX_PATH] = {};
		DWORD length = GetTempPathW(MAX_PATH, tempPath);
		path_ = (length > 0 && length < MAX_PATH) ? tempPath : L".\\";
		path_ += suiteName + L"-" + std::to_wstring(GetCurrentProcessId());
		CreateDirectoryW(path_.c_str(), nullptr);
	}

	const std::wstring& path() const {return path_;}

	// Returns the full path for a file inside the directory and remembers it
	// for removeAll(). The caller writes the file itself (or hands the path to
	// the code under test to write).
	std::wstring trackFile(const std::wstring& fileName)
	{
		std::wstring full = path_ + L"\\" + fileName;
		files_.push_back(full);
		return full;
	}

	// Registers an already-computed full path for removeAll(); for call sites
	// that build their paths through path() directly.
	void track(const std::wstring& fullPath)
	{
		files_.push_back(fullPath);
	}

	// Deletes every tracked file, then the directory (which only succeeds if
	// nothing untracked was left behind - deliberate, so leaks show up as a
	// leftover directory instead of disappearing).
	void removeAll()
	{
		for (const std::wstring& file : files_)
			DeleteFileW(file.c_str());
		files_.clear();
		RemoveDirectoryW(path_.c_str());
	}

private:
	std::wstring path_;
	std::vector<std::wstring> files_;
};

} // namespace test
