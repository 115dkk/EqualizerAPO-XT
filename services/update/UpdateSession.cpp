/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "UpdateSession.h"
#include "platform/windows/ThreadWait.h"

#include <chrono>
#include <exception>
#include <utility>

namespace
{
void reportCoordinatorError(const std::function<void(const std::string&)>& reportError,
	const std::string& error) noexcept
{
	if (!reportError)
		return;
	try
	{
		reportError(error);
	}
	catch (...)
	{
	}
}
}

bool shouldCheckAutomatically(bool automaticUpdates, bool firstRun, bool hasSession) noexcept
{
	return automaticUpdates && !firstRun && hasSession;
}

ManualCheckNotice manualCheckNotice(bool hasSession, UpdateDownloadStatus status) noexcept
{
	if (!hasSession)
		return ManualCheckNotice::NotInstalledCopy;

	switch (status)
	{
	case UpdateDownloadStatus::Idle:
	case UpdateDownloadStatus::Running:
		return ManualCheckNotice::Checking;
	case UpdateDownloadStatus::UpToDate:
		return ManualCheckNotice::UpToDate;
	case UpdateDownloadStatus::Staged:
		return ManualCheckNotice::Downloaded;
	case UpdateDownloadStatus::Cancelled:
	case UpdateDownloadStatus::Failed:
		return ManualCheckNotice::Failed;
	}
	return ManualCheckNotice::Failed;
}

UpdateApplyOutcome coordinatePendingRestartUpdate(IUpdateClient& client,
	const std::function<void(const std::string&)>& reportError)
{
	try
	{
		return client.applyPendingRestartUpdate()
			? UpdateApplyOutcome::UpdaterLaunched
			: UpdateApplyOutcome::NoUpdate;
	}
	catch (const std::exception& error)
	{
		reportCoordinatorError(reportError, error.what());
		return UpdateApplyOutcome::Failed;
	}
	catch (...)
	{
		reportCoordinatorError(reportError, "unknown pending restart update failure");
		return UpdateApplyOutcome::Failed;
	}
}

UpdateSession::UpdateSession(
	std::unique_ptr<IUpdateClient> client,
	ErrorReporter errorReporter)
	: client(std::move(client)), errorReporter(std::move(errorReporter))
{
}

UpdateSession::~UpdateSession()
{
	shutdown();
}

bool UpdateSession::startDownload()
{
	std::lock_guard<std::timed_mutex> workerLock(workerMutex);
	std::lock_guard<std::mutex> lock(mutex);
	if (started || shuttingDown)
		return false;

	started = true;
	startWorker();
	return true;
}

UpdateCheckStart UpdateSession::checkNow()
{
	std::lock_guard<std::timed_mutex> workerLock(workerMutex);
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (shuttingDown)
			return UpdateCheckStart::ShuttingDown;
		if (status == UpdateDownloadStatus::Running)
			return UpdateCheckStart::AlreadyRunning;
		if (pendingVersion.has_value())
			return UpdateCheckStart::AlreadyStaged;
	}

	// The previous worker takes mutex to publish its result. Keep only the
	// worker-lifecycle lock while joining so publication cannot deadlock here.
	if (worker.joinable())
		worker.join();

	std::lock_guard<std::mutex> lock(mutex);
	// Another thread may begin shutdown while this call joins the previous worker.
	// cppcheck-suppress knownConditionTrueFalse
	if (shuttingDown)
		return UpdateCheckStart::ShuttingDown;
	started = true;
	startWorker();
	return UpdateCheckStart::Started;
}

void UpdateSession::startWorker()
{
	pendingVersion.reset();
	status = UpdateDownloadStatus::Running;
	worker = std::thread([this]() { runWorker(); });
}

void UpdateSession::runWorker() noexcept
{
	try
	{
		std::optional<std::wstring> stagedVersion = client->stageAvailableUpdate();
		std::lock_guard<std::mutex> stateLock(mutex);
		if (status == UpdateDownloadStatus::Cancelled)
			return;
		pendingVersion = std::move(stagedVersion);
		status = pendingVersion.has_value()
			? UpdateDownloadStatus::Staged
			: UpdateDownloadStatus::UpToDate;
	}
	catch (const std::exception& error)
	{
		{
			std::lock_guard<std::mutex> stateLock(mutex);
			if (status == UpdateDownloadStatus::Cancelled)
				return;
			pendingVersion.reset();
			status = UpdateDownloadStatus::Failed;
		}
		reportError(error.what());
	}
	catch (...)
	{
		{
			std::lock_guard<std::mutex> stateLock(mutex);
			if (status == UpdateDownloadStatus::Cancelled)
				return;
			pendingVersion.reset();
			status = UpdateDownloadStatus::Failed;
		}
		reportError("unknown update download failure");
	}
}

void UpdateSession::cancelPendingDownload()
{
	std::lock_guard<std::mutex> lock(mutex);
	shuttingDown = true;
	if (status == UpdateDownloadStatus::Idle || status == UpdateDownloadStatus::Running)
	{
		// Velopack's C progress callback returns void and has no abort contract.
		// Do not throw across it. Join later, but never publish/apply a download
		// finishing after exit (audit #467 TD-64, testUpdateExitCancelsDownloadBeforePublication).
		status = UpdateDownloadStatus::Cancelled;
		pendingVersion.reset();
	}
}

bool UpdateSession::shutdown(unsigned long timeoutMs)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		shuttingDown = true;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	std::unique_lock<std::timed_mutex> workerLock(workerMutex, std::defer_lock);
	if (!workerLock.try_lock() && !workerLock.try_lock_until(deadline))
		return false;

	unsigned long remainingMs = 0;
	const auto now = std::chrono::steady_clock::now();
	if (now < deadline)
	{
		remainingMs = static_cast<unsigned long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
	}
	// Keep ownership on timeout: the process-exit caller must skip destruction,
	// not let a live download touch a freed session (audit #467 TD-58).
	return winutil::joinThreadFor(worker, remainingMs);
}

void UpdateSession::shutdown()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		shuttingDown = true;
	}
	std::lock_guard<std::timed_mutex> workerLock(workerMutex);
	if (worker.joinable())
		worker.join();
}

bool UpdateSession::hasPendingUpdate() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return pendingVersion.has_value();
}

std::wstring UpdateSession::pendingUpdateVersion() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return pendingVersion.value_or(std::wstring());
}

UpdateDownloadStatus UpdateSession::downloadStatus() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return status;
}

UpdateApplyOutcome UpdateSession::applyPendingUpdate(
	bool currentProcessElevated,
	const std::function<bool()>& launchElevatedCoordinator)
{
	shutdown();
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!pendingVersion.has_value())
			return UpdateApplyOutcome::NoUpdate;
	}

	try
	{
		if (!currentProcessElevated)
		{
			return launchElevatedCoordinator()
				? UpdateApplyOutcome::CoordinatorLaunched
				: UpdateApplyOutcome::Failed;
		}

		client->applyStagedUpdate();
		return UpdateApplyOutcome::UpdaterLaunched;
	}
	catch (const std::exception& error)
	{
		reportError(error.what());
		return UpdateApplyOutcome::Failed;
	}
	catch (...)
	{
		reportError("unknown update apply failure");
		return UpdateApplyOutcome::Failed;
	}
}

void UpdateSession::reportError(const std::string& error) const noexcept
{
	if (!errorReporter)
		return;
	try
	{
		errorReporter(error);
	}
	catch (...)
	{
	}
}
