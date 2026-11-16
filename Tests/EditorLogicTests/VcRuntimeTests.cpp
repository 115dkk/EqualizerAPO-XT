/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "EditorLogicTestSupport.h"
#include "services/install/VcRuntime.h"

TEST_CASE(EditorLogicTests, testVcRuntimeVersionDecisionAndRunner)
{
	using namespace vcruntime;
	const Version shipped{14, 51, 36231, 0};
	const Version older{14, 30, 30704, 0};
	const Version newer{14, 52, 1, 0};
	expectTrue(needsInstall(std::nullopt, shipped), QStringLiteral("missing VC runtime needs shipped installer"));
	expectTrue(needsInstall(older, shipped), QStringLiteral("older VC runtime needs shipped installer"));
	expectFalse(needsInstall(shipped, shipped), QStringLiteral("equal VC runtime is not reinstalled"));
	expectFalse(needsInstall(newer, shipped), QStringLiteral("newer VC runtime is not downgraded"));
	expectFalse(needsInstall(older, std::nullopt), QStringLiteral("absent shipped installer is skipped"));
	int calls = 0;
	const Runner success = [&]() { ++calls; return ProcessResult{true, 0}; };
	expectTrue(runIfNeeded(shipped, shipped, success) == Outcome::NotNeeded,
		QStringLiteral("equal runtime does not launch"));
	expectEqual(calls, 0, QStringLiteral("skipped runner has no side effects"));
	for (const unsigned long code : {0UL, 3010UL, 1638UL})
		expectTrue(runIfNeeded(older, shipped, [code]() { return ProcessResult{true, code}; }) == Outcome::Completed,
			QStringLiteral("success, reboot required and newer installed are successful outcomes"));
	expectTrue(runIfNeeded(older, shipped, []() { return ProcessResult{true, 1603}; }) == Outcome::Failed,
		QStringLiteral("installer failure is reported"));
	expectTrue(runIfNeeded(older, shipped, []() { return ProcessResult{}; }) == Outcome::Failed,
		QStringLiteral("launch failure is reported"));
	expectTrue(runIfNeeded(older, shipped, []() { return ProcessResult{true, std::nullopt}; }) == Outcome::Running,
		QStringLiteral("unfinished installer is not reported as completed"));
}
