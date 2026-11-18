/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"
#include "Editor/helpers/TroubleshootingNotices.h"

#include <limits>

TEST_CASE(EditorLogicTests, testEngineRestartOfferRequiresRunningStaleEngine)
{
	using eapo::ipc::beacon::EngineState;
	for (bool running : { false, true })
	{
		for (EngineState state : { EngineState::NotLoaded, EngineState::Current, EngineState::Stale })
			expectTrue(TroubleshootingNotices::shouldOfferEngineRestart(running, state)
				== (running && state == EngineState::Stale), "only a running stale engine offers a restart");
	}
}

TEST_CASE(EditorLogicTests, testAudioRestartNoticesRespectDeclineAndFailure)
{
	using TroubleshootingNotices::RestartNotice;
	using audioengine::RestartRequest;
	expectTrue(TroubleshootingNotices::restartNotice(RestartRequest::Restarted) == RestartNotice::Restarted,
		"a completed restart reports success");
	expectTrue(TroubleshootingNotices::restartNotice(RestartRequest::Declined) == RestartNotice::None,
		"declining elevation produces no notice");
	expectTrue(TroubleshootingNotices::restartNotice(RestartRequest::Failed) == RestartNotice::Failed,
		"a failed restart reports failure");
	expectTrue(RestartNotice{} == RestartNotice::Failed,
		"an exception in BackgroundProbe cannot default to a successful restart");
}

TEST_CASE(EditorLogicTests, testManualUpdateCheckTransitions)
{
	using TroubleshootingNotices::UpdateAction;
	for (UpdateDownloadStatus status : { UpdateDownloadStatus::Idle, UpdateDownloadStatus::Running,
		UpdateDownloadStatus::UpToDate, UpdateDownloadStatus::Staged,
		UpdateDownloadStatus::Cancelled, UpdateDownloadStatus::Failed })
	{
		expectTrue(TroubleshootingNotices::updateAction(UpdateCheckStart::ShuttingDown, status) == UpdateAction::None,
			"shutdown never polls or opens a notice");
		expectTrue(TroubleshootingNotices::updateAction(UpdateCheckStart::AlreadyStaged, status) == UpdateAction::ShowNotice,
			"an already staged update shows its notice immediately");
		for (UpdateCheckStart start : { UpdateCheckStart::Started, UpdateCheckStart::AlreadyRunning })
			expectTrue(TroubleshootingNotices::updateAction(start, status)
				== (status == UpdateDownloadStatus::Running ? UpdateAction::Poll : UpdateAction::ShowNotice),
				"requested checks poll only while running, including a check that finishes before the first poll");
	}
}

TEST_CASE(EditorLogicTests, testEngineVersionFormatting)
{
	expectEqual(TroubleshootingNotices::formatVersion({ 2, 42, 7 }), QStringLiteral("2.42.7"),
		"engine versions contain three decimal components");
	expectEqual(TroubleshootingNotices::formatVersion({}), QStringLiteral("0.0.0"), "zero components are retained");
	const unsigned maximum = (std::numeric_limits<unsigned>::max)();
	expectEqual(TroubleshootingNotices::formatVersion({ maximum, maximum, maximum }),
		QString::number(maximum) + '.' + QString::number(maximum) + '.' + QString::number(maximum),
		"wire version components do not become signed or lose digits");
}
