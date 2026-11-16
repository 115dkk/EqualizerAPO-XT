/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include "platform/windows/ElevatedLaunch.h"
#include "services/install/ApoRegistration.h"

TEST_CASE(EditorLogicTests, testRegistrationRepairNoticeMatchesTheRepairResult)
{
	ApoRegistration::Verification verification;
	harness.expectTrue(ApoRegistration::repairNotice(verification) == ApoRegistration::RepairNotice::Repaired,
		"a repaired invalid registration selects the success notice");
	verification.repairResult = ApoRegistration::Result::RegistrationFailed;
	harness.expectTrue(ApoRegistration::repairNotice(verification) == ApoRegistration::RepairNotice::Failed,
		"a failed registration repair selects the failure notice");
	verification.protectedAudioDG = true;
	verification.registration = true;
	harness.expectTrue(ApoRegistration::repairNotice(verification) == ApoRegistration::RepairNotice::None,
		"an already valid registration needs no notice");
}

TEST_CASE(EditorLogicTests, testElevatedLaunchMessageDistinguishesADeclineFromAnotherFailure)
{
	winutil::ElevatedLaunch launch;
	launch.error = ERROR_CANCELLED;
	harness.expectTrue(winutil::elevatedLaunchMessage(launch) == winutil::ElevatedLaunchMessage::ApprovalDeclined,
		"ERROR_CANCELLED selects the approval-declined message");
	launch.error = ERROR_FILE_NOT_FOUND;
	harness.expectTrue(winutil::elevatedLaunchMessage(launch) == winutil::ElevatedLaunchMessage::Failed,
		"another ShellExecute error selects the Win32 failure message");
	launch.started = true;
	launch.error = ERROR_SUCCESS;
	harness.expectTrue(winutil::elevatedLaunchMessage(launch) == winutil::ElevatedLaunchMessage::None,
		"a started process needs no failure message");
}
