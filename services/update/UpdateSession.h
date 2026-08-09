/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class IUpdateClient
{
public:
	virtual ~IUpdateClient() = default;

	virtual std::optional<std::wstring> stageAvailableUpdate() = 0;
	virtual void applyStagedUpdate() = 0;
	virtual bool applyPendingRestartUpdate() = 0;
};

enum class UpdateApplyOutcome
{
	NoUpdate,
	CoordinatorLaunched,
	UpdaterLaunched,
	Failed
};

enum class UpdateDownloadStatus
{
	Idle,
	Running,
	UpToDate,
	Staged,
	Failed
};

UpdateApplyOutcome coordinatePendingRestartUpdate(IUpdateClient& client);

class UpdateSession
{
public:
	using ErrorReporter = std::function<void(const std::string&)>;

	explicit UpdateSession(
		std::unique_ptr<IUpdateClient> client,
		ErrorReporter errorReporter = ErrorReporter());
	UpdateSession(const UpdateSession&) = delete;
	UpdateSession& operator=(const UpdateSession&) = delete;
	~UpdateSession();

	bool startDownload();
	void shutdown();
	bool hasPendingUpdate() const;
	std::wstring pendingUpdateVersion() const;
	UpdateDownloadStatus downloadStatus() const;
	UpdateApplyOutcome applyPendingUpdate(
		bool currentProcessElevated,
		const std::function<bool()>& launchElevatedCoordinator);

private:
	void reportError(const std::string& error) const noexcept;

	std::unique_ptr<IUpdateClient> client;
	ErrorReporter errorReporter;
	mutable std::mutex mutex;
	std::thread worker;
	bool started = false;
	std::optional<std::wstring> pendingVersion;
	UpdateDownloadStatus status = UpdateDownloadStatus::Idle;
};
