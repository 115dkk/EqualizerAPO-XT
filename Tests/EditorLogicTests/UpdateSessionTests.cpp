/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "services/update/UpdateSession.h"
#include "services/update/VelopackBootstrap.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
struct FakeUpdateState
{
	int stageCalls = 0;
	int applyCalls = 0;
	int pendingRestartApplyCalls = 0;
	bool throwDuringStage = false;
	bool throwDuringApply = false;
	bool throwDuringPendingRestartApply = false;
	bool pendingRestartAvailable = false;
	std::optional<std::wstring> availableVersion = L"9.9.9";
};

class FakeUpdateClient : public IUpdateClient
{
public:
	explicit FakeUpdateClient(std::shared_ptr<FakeUpdateState> state)
		: state(std::move(state))
	{
	}

	std::optional<std::wstring> stageAvailableUpdate() override
	{
		++state->stageCalls;
		if (state->throwDuringStage)
			throw std::runtime_error("stage failed");
		return state->availableVersion;
	}

	void applyStagedUpdate() override
	{
		++state->applyCalls;
		if (state->throwDuringApply)
			throw std::runtime_error("apply failed");
	}

	bool applyPendingRestartUpdate() override
	{
		++state->pendingRestartApplyCalls;
		if (state->throwDuringPendingRestartApply)
			throw std::runtime_error("pending restart apply failed");
		return state->pendingRestartAvailable;
	}

private:
	std::shared_ptr<FakeUpdateState> state;
};
}

void testUpdateCoordinatorAppliesPendingRestartThroughAdapter()
{
	auto state = std::make_shared<FakeUpdateState>();
	state->pendingRestartAvailable = true;
	FakeUpdateClient client(state);

	const UpdateApplyOutcome outcome = coordinatePendingRestartUpdate(client);

	expectTrue(
		outcome == UpdateApplyOutcome::UpdaterLaunched,
		QStringLiteral("the coordinator reports that the updater was launched"));
	expectEqual(
		state->pendingRestartApplyCalls,
		1,
		QStringLiteral("the coordinator reopens the staged package through its adapter"));
}

void testUpdateSessionReportsUpToDateAndStartsOnlyOnce()
{
	auto state = std::make_shared<FakeUpdateState>();
	state->availableVersion.reset();
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));

	expectTrue(session.startDownload(), QStringLiteral("the first download request starts"));
	expectFalse(session.startDownload(), QStringLiteral("a repeated download request is ignored"));
	session.shutdown();

	expectEqual(state->stageCalls, 1, QStringLiteral("the adapter is called once"));
	expectTrue(
		session.downloadStatus() == UpdateDownloadStatus::UpToDate,
		QStringLiteral("no available version is reported as up to date"));
	expectFalse(session.hasPendingUpdate(), QStringLiteral("up to date does not publish pending state"));
	int coordinatorLaunches = 0;
	expectTrue(
		session.applyPendingUpdate(false, [&coordinatorLaunches]() {
			++coordinatorLaunches;
			return true;
		}) == UpdateApplyOutcome::NoUpdate,
		QStringLiteral("an up-to-date session does not ask its caller to exit"));
	expectEqual(coordinatorLaunches, 0, QStringLiteral("no staged update means no elevation prompt"));
}

void testUpdateSessionAppliesDirectlyWhenAlreadyElevated()
{
	auto state = std::make_shared<FakeUpdateState>();
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));
	session.startDownload();
	session.shutdown();

	int coordinatorLaunches = 0;
	const UpdateApplyOutcome outcome = session.applyPendingUpdate(true, [&coordinatorLaunches]() {
		++coordinatorLaunches;
		return true;
	});

	expectTrue(
		outcome == UpdateApplyOutcome::UpdaterLaunched,
		QStringLiteral("an elevated Editor reports that Update.exe was launched"));
	expectEqual(state->applyCalls, 1, QStringLiteral("the elevated Editor applies through its adapter"));
	expectEqual(coordinatorLaunches, 0, QStringLiteral("an elevated Editor does not request another prompt"));
}

void testUpdateSessionContainsApplyFailure()
{
	auto state = std::make_shared<FakeUpdateState>();
	state->throwDuringApply = true;
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));
	session.startDownload();
	session.shutdown();

	const UpdateApplyOutcome outcome = session.applyPendingUpdate(true, []() { return true; });

	expectTrue(
		outcome == UpdateApplyOutcome::Failed,
		QStringLiteral("an SDK apply exception is returned to main instead of exiting"));
	expectEqual(state->applyCalls, 1, QStringLiteral("the failing apply is attempted once"));
}

void testUpdateSessionKeepsPendingUpdateWhenElevationIsCancelled()
{
	auto state = std::make_shared<FakeUpdateState>();
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));
	session.startDownload();
	session.shutdown();

	const UpdateApplyOutcome outcome = session.applyPendingUpdate(false, []() { return false; });

	expectTrue(
		outcome == UpdateApplyOutcome::Failed,
		QStringLiteral("a cancelled elevation request returns without terminating the Editor"));
	expectTrue(session.hasPendingUpdate(), QStringLiteral("the staged package remains available after cancellation"));
	expectEqual(state->applyCalls, 0, QStringLiteral("cancellation never invokes the SDK apply path"));
}

void testUpdateCoordinatorReportsNoPendingRestart()
{
	auto state = std::make_shared<FakeUpdateState>();
	FakeUpdateClient client(state);

	const UpdateApplyOutcome outcome = coordinatePendingRestartUpdate(client);

	expectTrue(
		outcome == UpdateApplyOutcome::NoUpdate,
		QStringLiteral("the coordinator distinguishes an absent staged package"));
	expectEqual(state->pendingRestartApplyCalls, 1, QStringLiteral("the pending package is probed once"));
}

void testUpdateCoordinatorContainsAdapterFailure()
{
	auto state = std::make_shared<FakeUpdateState>();
	state->throwDuringPendingRestartApply = true;
	FakeUpdateClient client(state);

	const UpdateApplyOutcome outcome = coordinatePendingRestartUpdate(client);

	expectTrue(
		outcome == UpdateApplyOutcome::Failed,
		QStringLiteral("a coordinator SDK exception becomes a normal failure result"));
	expectEqual(state->pendingRestartApplyCalls, 1, QStringLiteral("the failing coordinator apply is attempted once"));
}

void testUpdateSessionContainsBackgroundFailure()
{
	auto state = std::make_shared<FakeUpdateState>();
	state->throwDuringStage = true;
	std::string reportedError;
	UpdateSession session(
		std::make_unique<FakeUpdateClient>(state),
		[&reportedError](const std::string& error) { reportedError = error; });

	session.startDownload();
	session.shutdown();

	expectTrue(
		session.downloadStatus() == UpdateDownloadStatus::Failed,
		QStringLiteral("an adapter exception becomes an observable download failure"));
	expectFalse(session.hasPendingUpdate(), QStringLiteral("a failed download never publishes pending state"));
	expectEqual(
		QString::fromStdString(reportedError),
		QStringLiteral("stage failed"),
		QStringLiteral("the background failure reaches the injected diagnostic reporter"));
}

void testUpdateSessionLaunchesElevatedCoordinatorWithoutApplyingDirectly()
{
	auto state = std::make_shared<FakeUpdateState>();
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));
	session.startDownload();
	session.shutdown();

	int coordinatorLaunches = 0;
	const UpdateApplyOutcome outcome = session.applyPendingUpdate(false, [&coordinatorLaunches]() {
		++coordinatorLaunches;
		return true;
	});

	expectTrue(
		outcome == UpdateApplyOutcome::CoordinatorLaunched,
		QStringLiteral("an unelevated session asks its caller to exit after launching the coordinator"));
	expectEqual(coordinatorLaunches, 1, QStringLiteral("the elevated coordinator launches once"));
	expectEqual(state->applyCalls, 0, QStringLiteral("the unelevated process never applies directly"));
}

void testUpdateSessionPublishesStagedVersionAfterJoin()
{
	auto state = std::make_shared<FakeUpdateState>();
	UpdateSession session(std::make_unique<FakeUpdateClient>(state));

	expectTrue(session.startDownload(), QStringLiteral("the update worker starts once"));
	session.shutdown();

	expectEqual(state->stageCalls, 1, QStringLiteral("the update adapter stages exactly once"));
	expectTrue(session.hasPendingUpdate(), QStringLiteral("the staged update becomes observable after join"));
	expectEqual(
		QString::fromStdWString(session.pendingUpdateVersion()),
		QStringLiteral("9.9.9"),
		QStringLiteral("the staged version survives worker shutdown"));
}

void testVelopackInstallRootFollowsTheCurrentLeafRule()
{
	// Audit #275 TD-29: the 'leaf "current" means its parent is the install
	// root' rule is pure string logic; pin it here so the Velopack layout
	// assumption cannot drift silently.
	using VB = VelopackBootstrap;
	expectEqual(
		QString::fromStdWString(VB::installRootFromBinDir(L"C:\\Apps\\EqualizerAPO-XT\\current")),
		QStringLiteral("C:\\Apps\\EqualizerAPO-XT"),
		QStringLiteral("a bin dir whose leaf is 'current' resolves to its parent"));
	expectEqual(
		QString::fromStdWString(VB::installRootFromBinDir(L"C:\\Apps\\EqualizerAPO-XT\\CURRENT")),
		QStringLiteral("C:\\Apps\\EqualizerAPO-XT"),
		QStringLiteral("the leaf comparison is case-insensitive"));
	expectEqual(
		QString::fromStdWString(VB::installRootFromBinDir(L"C:/Apps/EqualizerAPO-XT/current")),
		QStringLiteral("C:/Apps/EqualizerAPO-XT"),
		QStringLiteral("forward slashes separate the leaf too"));
	expectEqual(
		QString::fromStdWString(VB::installRootFromBinDir(L"C:\\Apps\\EqualizerAPO-XT\\bin")),
		QStringLiteral("C:\\Apps\\EqualizerAPO-XT\\bin"),
		QStringLiteral("a non-Velopack layout is its own root"));
	expectEqual(
		QString::fromStdWString(VB::installRootFromBinDir(L"current")),
		QStringLiteral("current"),
		QStringLiteral("a bare path without separators stays as-is"));
	expectTrue(
		VB::installRootFromBinDir(std::wstring()).empty(),
		QStringLiteral("an empty bin dir resolves to an empty root"));
}

void testElevatedCoordinatorArgumentHasOneSpelling()
{
	// Audit #275 TD-02: the parse side reads the narrow constant and the
	// ShellExecuteExW side passes the wide one; both must be the same text.
	const std::string narrow = VelopackBootstrap::kElevatedCoordinatorArgument;
	const std::wstring wide = VelopackBootstrap::kElevatedCoordinatorArgumentW;
	expectEqual(
		QString::fromStdWString(wide),
		QString::fromStdString(narrow),
		QStringLiteral("the elevated coordinator argument has a single spelling"));
}
