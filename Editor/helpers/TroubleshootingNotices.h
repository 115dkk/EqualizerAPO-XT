/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include "runtime/ipc/EngineBeacon.h"
#include "services/update/UpdateSession.h"
#include "services/windows/AudioEngineRestart.h"

namespace TroubleshootingNotices
{
inline bool shouldOfferEngineRestart(bool audioServiceRunning, eapo::ipc::beacon::EngineState state) noexcept
{
	return audioServiceRunning && state == eapo::ipc::beacon::EngineState::Stale;
}

// BackgroundProbe value-initializes its result if the work throws.
enum class RestartNotice { Failed, None, Restarted };

inline RestartNotice restartNotice(audioengine::RestartRequest result) noexcept
{
	switch (result)
	{
	case audioengine::RestartRequest::Restarted:
		return RestartNotice::Restarted;
	case audioengine::RestartRequest::Declined:
		return RestartNotice::None;
	case audioengine::RestartRequest::Failed:
		return RestartNotice::Failed;
	}
	return RestartNotice::Failed;
}

enum class UpdateAction { None, Poll, ShowNotice };

inline UpdateAction updateAction(UpdateCheckStart start, UpdateDownloadStatus status) noexcept
{
	if (start == UpdateCheckStart::ShuttingDown)
		return UpdateAction::None;
	if (start == UpdateCheckStart::AlreadyStaged)
		return UpdateAction::ShowNotice;
	return status == UpdateDownloadStatus::Running ? UpdateAction::Poll : UpdateAction::ShowNotice;
}

inline QString formatVersion(const eapo::ipc::beacon::EngineVersion& version)
{
	return QString::number(version.major) + '.' + QString::number(version.minor) + '.' + QString::number(version.revision);
}
}
