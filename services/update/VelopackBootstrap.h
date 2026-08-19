/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2025  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <memory>
#include <string>
#include <wchar.h>

class UpdateSession;

// Process integration for the Editor's Velopack runtime. The long-lived update
// state belongs to an UpdateSession owned by main.cpp; this Module only creates
// the concrete SDK Adapter and handles the short-lived elevated coordinator.
// Single spelling for the elevated-coordinator argument. The narrow constant
// is what main.cpp parses; the wide constant is what ShellExecuteExW passes.
// Both derive from this macro so they cannot drift apart (audit #275 TD-02).
#define EAPO_ELEVATED_COORDINATOR_ARGUMENT "--eapo-apply-update-elevated"

class VelopackBootstrap
{
public:
	inline static constexpr char kElevatedCoordinatorArgument[] =
		EAPO_ELEVATED_COORDINATOR_ARGUMENT;
	inline static constexpr wchar_t kElevatedCoordinatorArgumentW[] =
		L"" EAPO_ELEVATED_COORDINATOR_ARGUMENT;

	// Pure path rule behind installRoot(): a Velopack install runs the binaries
	// from <root>\current, so a bin directory whose leaf is "current" resolves
	// to its parent and anything else is its own root. Header-inline so
	// EditorLogicTests can pin the rule without linking the SDK adapter
	// (audit #275 TD-29).
	static std::wstring installRootFromBinDir(const std::wstring& binDir)
	{
		if (binDir.empty())
			return std::wstring();
		size_t slash = binDir.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return binDir;
		std::wstring leaf = binDir.substr(slash + 1);
		if (_wcsicmp(leaf.c_str(), L"current") != 0)
			return binDir;
		return binDir.substr(0, slash);
	}

	static bool isFirstRun();
	static bool isRestartingAfterUpdate();

	static bool isVelopackInstall();
	static std::wstring updateExePath();
	static std::wstring installRoot();
	static std::wstring currentBinDir();

	static std::unique_ptr<UpdateSession> createUpdateSession(
		const std::string& repoUrl,
		const std::string& channel = std::string());

	static bool launchElevatedUpdateCoordinator();

	// Entry point for the short-lived elevated coordinator. It reopens the
	// already-downloaded package through the same SDK Adapter used by the
	// regular session, starts Update.exe, then returns an exit status.
	static int runElevatedUpdateCoordinator(
		const std::string& repoUrl,
		const std::string& channel = std::string());
};
