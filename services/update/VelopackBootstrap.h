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

class UpdateSession;

// Process integration for the Editor's Velopack runtime. The long-lived update
// state belongs to an UpdateSession owned by main.cpp; this Module only creates
// the concrete SDK Adapter and handles the short-lived elevated coordinator.
class VelopackBootstrap
{
public:
	inline static constexpr char kElevatedCoordinatorArgument[] =
		"--eapo-apply-update-elevated";

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
