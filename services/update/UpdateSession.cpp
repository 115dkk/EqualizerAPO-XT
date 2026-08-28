/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "UpdateSession.h"

#include <exception>
#include <utility>

UpdateApplyOutcome coordinatePendingRestartUpdate(IUpdateClient& client)
{
	try
	{
		return client.applyPendingRestartUpdate()
			? UpdateApplyOutcome::UpdaterLaunched
			: UpdateApplyOutcome::NoUpdate;
	}
	catch (...)
	{
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
	std::lock_guard<std::mutex> lock(mutex);
	if (started)
		return false;

	started = true;
	status = UpdateDownloadStatus::Running;
	worker = std::thread([this]() {
		try
		{
			std::optional<std::wstring> stagedVersion = client->stageAvailableUpdate();
			std::lock_guard<std::mutex> stateLock(mutex);
			pendingVersion = std::move(stagedVersion);
			status = pendingVersion.has_value()
				? UpdateDownloadStatus::Staged
				: UpdateDownloadStatus::UpToDate;
		}
		catch (const std::exception& error)
		{
			{
				std::lock_guard<std::mutex> stateLock(mutex);
				pendingVersion.reset();
				status = UpdateDownloadStatus::Failed;
			}
			reportError(error.what());
		}
		catch (...)
		{
			{
				std::lock_guard<std::mutex> stateLock(mutex);
				pendingVersion.reset();
				status = UpdateDownloadStatus::Failed;
			}
			reportError("unknown update download failure");
		}
	});
	return true;
}

void UpdateSession::shutdown()
{
	std::thread ownedWorker;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!worker.joinable())
			return;
		ownedWorker = std::move(worker);
	}
	ownedWorker.join();
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
