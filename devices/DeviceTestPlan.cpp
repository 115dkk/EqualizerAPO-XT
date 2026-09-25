/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/DeviceTestPlan.h"

#include <algorithm>

#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceTestWire.h"

static_assert(static_cast<int>(DeviceTestMode::LfxGfx) == DeviceAPOInfo::INSTALL_LFX_GFX
	&& static_cast<int>(DeviceTestMode::SfxMfx) == DeviceAPOInfo::INSTALL_SFX_MFX
	&& static_cast<int>(DeviceTestMode::SfxEfx) == DeviceAPOInfo::INSTALL_SFX_EFX,
	"DeviceTestMode is numbered as DeviceAPOInfo::InstallMode");

std::optional<DeviceTestStage> deviceTestStageFromWire(std::string_view value)
{
	if (value == devicetest::wire::kStagePreMix)
		return DeviceTestStage::PreMix;
	if (value == devicetest::wire::kStagePostMix)
		return DeviceTestStage::PostMix;
	return std::nullopt;
}

std::optional<DeviceTestPhase> deviceTestPhaseFromWire(std::string_view value)
{
	if (value == devicetest::wire::kPhaseInitialize)
		return DeviceTestPhase::Initialize;
	if (value == devicetest::wire::kPhaseChildApo)
		return DeviceTestPhase::ChildApo;
	return std::nullopt;
}

const char* deviceTestModeName(DeviceTestMode mode)
{
	switch (mode)
	{
	case DeviceTestMode::LfxGfx:
		return "LFX/GFX";
	case DeviceTestMode::SfxMfx:
		return "SFX/MFX";
	case DeviceTestMode::SfxEfx:
		return "SFX/EFX";
	}
	return "";
}

DeviceTestSelection deviceTestSelectionOf(DeviceAPOInfo& info)
{
	const DeviceAPOInfo::InstallState& state = info.getSelectedInstallState();
	DeviceTestSelection selection;
	selection.installPreMix = state.installPreMix;
	selection.installPostMix = state.installPostMix;
	selection.useOriginalApoPreMix = state.useOriginalAPOPreMix;
	selection.useOriginalApoPostMix = state.useOriginalAPOPostMix;
	selection.autoAdjust = state.autoAdjust;
	selection.mode = static_cast<DeviceTestMode>(state.installMode);
	selection.isInput = info.isInput();
	selection.hasOriginalApoPreMix = !info.getOriginalAPOPreMix().empty();
	selection.hasOriginalApoPostMix = !info.getOriginalAPOPostMix().empty();
	return selection;
}

int DeviceTestPlan::Result::score() const
{
	int score = 0;
	if (preMixOk)
		score += 20;
	if (postMixOk)
		score += 10;
	if (!childApoPreMixOk)
		score -= 2;
	if (!childApoPostMixOk)
		score -= 1;
	return score;
}

DeviceTestPlan::DeviceTestPlan(const DeviceTestSelection& selection)
	: bestMode(selection.mode),
	wantsOriginalApoPreMix(selection.useOriginalApoPreMix || !selection.hasOriginalApoPreMix),
	wantsOriginalApoPostMix(selection.useOriginalApoPostMix || !selection.hasOriginalApoPostMix)
{
	// The minimum supported Windows (10 1809) has every slot, so automatic
	// adjustment walks all three modes.
	if (selection.autoAdjust)
		remaining = {DeviceTestMode::SfxEfx, DeviceTestMode::SfxMfx, DeviceTestMode::LfxGfx};
	else
		remaining = {selection.mode};
}

void DeviceTestPlan::beginAttempt(const DeviceTestSelection& selection)
{
	const auto it = std::find(remaining.begin(), remaining.end(), selection.mode);
	if (it != remaining.end())
		remaining.erase(it);
	current.childApoPreMixOk = !selection.useOriginalApoPreMix || !selection.hasOriginalApoPreMix;
	current.childApoPostMixOk = !selection.useOriginalApoPostMix || !selection.hasOriginalApoPostMix;
}

std::optional<DeviceTestStage> DeviceTestPlan::record(DeviceTestStage stage, std::optional<DeviceTestPhase> phase)
{
	const bool preMix = stage == DeviceTestStage::PreMix;
	bool& ok = preMix ? current.preMixOk : current.postMixOk;
	bool& childOk = preMix ? current.childApoPreMixOk : current.childApoPostMixOk;
	if (phase == DeviceTestPhase::Initialize)
		ok = true;
	else if (phase == DeviceTestPhase::ChildApo)
		childOk = true;
	if (ok && childOk)
		return stage;
	return std::nullopt;
}

bool DeviceTestPlan::satisfied(const DeviceTestSelection& selection) const
{
	const bool preMixDone = (current.preMixOk && current.childApoPreMixOk) || !expectsPreMix(selection);
	const bool postMixDone = (current.postMixOk && current.childApoPostMixOk) || !expectsPostMix(selection);
	return preMixDone && postMixDone;
}

DeviceTestPlan::Fallback DeviceTestPlan::fallBack(const DeviceTestSelection& selection)
{
	if (current.score() > best.score())
	{
		bestMode = selection.mode;
		best = current;
	}

	Fallback fallback;
	if (!remaining.empty())
	{
		fallback.mode = remaining.front();
		fallback.shown = current;
		current = Result();
	}
	else
	{
		fallback.mode = bestMode;
		fallback.shown = best;
		fallback.givesUp = true;
	}
	return fallback;
}

bool DeviceTestPlan::chainsOriginalApoPreMix(bool hasOriginalApoInNewMode) const
{
	return wantsOriginalApoPreMix && hasOriginalApoInNewMode;
}

bool DeviceTestPlan::chainsOriginalApoPostMix(bool hasOriginalApoInNewMode) const
{
	return wantsOriginalApoPostMix && hasOriginalApoInNewMode;
}

bool DeviceTestPlan::expectsPreMix(const DeviceTestSelection& selection)
{
	return selection.installPreMix;
}

bool DeviceTestPlan::expectsPostMix(const DeviceTestSelection& selection)
{
	return selection.installPostMix && !selection.isInput;
}

DeviceTestItemStatus DeviceTestPlan::statusFor(const Result& result, DeviceTestStage stage,
	const DeviceTestSelection& selection)
{
	const bool preMix = stage == DeviceTestStage::PreMix;
	const bool ok = preMix ? result.preMixOk : result.postMixOk;
	const bool childOk = preMix ? result.childApoPreMixOk : result.childApoPostMixOk;
	const bool useOriginal = preMix ? selection.useOriginalApoPreMix : selection.useOriginalApoPostMix;
	if (!ok)
		return DeviceTestItemStatus::Error;
	return childOk || !useOriginal ? DeviceTestItemStatus::Success : DeviceTestItemStatus::Warning;
}
