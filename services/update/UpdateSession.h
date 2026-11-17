/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

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
	Cancelled,
	Failed
};

enum class UpdateCheckStart
{
	Started,
	AlreadyRunning,
	AlreadyStaged,
	ShuttingDown
};

// The background check 60 s after launch runs only when the user left
// automatic updates on and this is not the first run after install.
bool shouldCheckAutomatically(bool automaticUpdates, bool firstRun, bool hasSession) noexcept;

enum class ManualCheckNotice
{
	NotInstalledCopy,
	Checking,
	UpToDate,
	Downloaded,
	Failed
};

// What the menu action tells the user once a requested check has settled (or
// cannot start).
ManualCheckNotice manualCheckNotice(bool hasSession, UpdateDownloadStatus status) noexcept;

// A failure from the client becomes Failed; its message goes to reportError
// when one is given, because the elevated coordinator has no window to show
// it in and would otherwise leave no trace of why the update did not apply.
UpdateApplyOutcome coordinatePendingRestartUpdate(IUpdateClient& client,
	const std::function<void(const std::string&)>& reportError = {});

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
	// Starts a check on request. Unlike startDownload it may run again after an
	// earlier run ended UpToDate, Failed or Cancelled.
	UpdateCheckStart checkNow();
	// Exit cancels an unfinished download, but keeps an already staged update.
	void cancelPendingDownload();
	void shutdown();
	// Serialize shutdown calls with startDownload. False keeps the worker owned;
	// the process-exit caller must hard-exit without destroying this session.
	bool shutdown(unsigned long timeoutMs);
	bool hasPendingUpdate() const;
	std::wstring pendingUpdateVersion() const;
	UpdateDownloadStatus downloadStatus() const;
	UpdateApplyOutcome applyPendingUpdate(
		bool currentProcessElevated,
		const std::function<bool()>& launchElevatedCoordinator);

private:
	void startWorker();
	void runWorker() noexcept;
	void reportError(const std::string& error) const noexcept;

	std::unique_ptr<IUpdateClient> client;
	ErrorReporter errorReporter;
	mutable std::mutex mutex;
	std::timed_mutex workerMutex;
	std::thread worker;
	bool started = false;
	bool shuttingDown = false;
	std::optional<std::wstring> pendingVersion;
	UpdateDownloadStatus status = UpdateDownloadStatus::Idle;
};
