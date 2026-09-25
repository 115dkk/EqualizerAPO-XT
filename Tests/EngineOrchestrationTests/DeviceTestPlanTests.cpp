/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the device test decides for one device (devices/DeviceTestPlan.h,
	audit #348 F15), table by table: the ladder of install modes, when a
	device counts as working, what each row shows, where it falls back to, and
	the wire vocabulary both sides now read from DeviceTestWire.h. The capture
	gate in CI runs the same decisions against a real endpoint; these pin them
	without one.
*/

#include <string>
#include <vector>

#include "devices/DeviceTestPlan.h"
#include "devices/DeviceTestWire.h"
#include "Tests/TestHarness.h"

namespace
{
DeviceTestSelection selectionOf(DeviceTestMode mode, bool autoAdjust)
{
	DeviceTestSelection selection;
	selection.installPreMix = true;
	selection.installPostMix = true;
	selection.autoAdjust = autoAdjust;
	selection.mode = mode;
	return selection;
}

void testLadder(test::Harness& harness)
{
	struct Case
	{
		const char* name = nullptr;
		DeviceTestMode selected = DeviceTestMode::LfxGfx;
		bool autoAdjust = false;
		std::vector<DeviceTestMode> ladder;
	};
	const Case cases[] = {
		{"automatic adjustment walks every mode, SFX/EFX first and LFX/GFX last", DeviceTestMode::LfxGfx, true,
			{DeviceTestMode::SfxEfx, DeviceTestMode::SfxMfx, DeviceTestMode::LfxGfx}},
		{"the ladder does not depend on the selected mode", DeviceTestMode::SfxMfx, true,
			{DeviceTestMode::SfxEfx, DeviceTestMode::SfxMfx, DeviceTestMode::LfxGfx}},
		{"without it only the selected mode is tried", DeviceTestMode::SfxMfx, false,
			{DeviceTestMode::SfxMfx}},
	};
	for (const Case& c : cases)
	{
		const DeviceTestPlan plan(selectionOf(c.selected, c.autoAdjust));
		harness.expect(plan.remainingModes() == c.ladder, c.name);
	}

	// The attempt in the selected mode takes that mode off the ladder, not
	// the first one.
	DeviceTestPlan plan(selectionOf(DeviceTestMode::SfxMfx, true));
	plan.beginAttempt(selectionOf(DeviceTestMode::SfxMfx, true));
	harness.expect(plan.remainingModes() == std::vector<DeviceTestMode>{DeviceTestMode::SfxEfx, DeviceTestMode::LfxGfx},
		"starting an attempt removes the mode it runs in");
}

void testSatisfaction(test::Harness& harness)
{
	struct Case
	{
		const char* name = nullptr;
		bool installPreMix = false;
		bool installPostMix = false;
		bool isInput = false;
		bool useOriginalPreMix = false;
		std::vector<std::pair<DeviceTestStage, DeviceTestPhase>> messages;
		bool satisfied = false;
	};
	using S = DeviceTestStage;
	using P = DeviceTestPhase;
	const Case cases[] = {
		{"nothing heard is not working", true, true, false, false, {}, false},
		{"both stages initialised is working", true, true, false, false,
			{{S::PreMix, P::Initialize}, {S::PostMix, P::Initialize}}, true},
		{"one stage of two is not enough", true, true, false, false, {{S::PreMix, P::Initialize}}, false},
		{"a stage that is not installed is not waited for", true, false, false, false, {{S::PreMix, P::Initialize}}, true},
		{"a capture endpoint has no post-mix stage to wait for", true, true, true, false, {{S::PreMix, P::Initialize}}, true},
		{"a chained driver APO has to report as well", true, false, false, true, {{S::PreMix, P::Initialize}}, false},
		{"and then it is working", true, false, false, true,
			{{S::PreMix, P::Initialize}, {S::PreMix, P::ChildApo}}, true},
		{"the child APO alone is not the APO", true, false, false, true, {{S::PreMix, P::ChildApo}}, false},
	};
	for (const Case& c : cases)
	{
		DeviceTestSelection selection = selectionOf(DeviceTestMode::SfxEfx, false);
		selection.installPreMix = c.installPreMix;
		selection.installPostMix = c.installPostMix;
		selection.isInput = c.isInput;
		selection.useOriginalApoPreMix = c.useOriginalPreMix;
		selection.hasOriginalApoPreMix = c.useOriginalPreMix;
		DeviceTestPlan plan(selection);
		plan.beginAttempt(selection);
		for (const auto& message : c.messages)
			plan.record(message.first, message.second);
		harness.expect(plan.satisfied(selection) == c.satisfied, c.name);
	}

	// A row turns to success on the message that completes it, and a phase
	// this build does not know changes nothing.
	DeviceTestSelection chained = selectionOf(DeviceTestMode::SfxEfx, false);
	chained.useOriginalApoPreMix = true;
	chained.hasOriginalApoPreMix = true;
	DeviceTestPlan plan(chained);
	plan.beginAttempt(chained);
	harness.expect(!plan.record(DeviceTestStage::PreMix, DeviceTestPhase::Initialize).has_value(),
		"the APO alone does not complete a stage that chains to the driver's APO");
	harness.expect(!plan.record(DeviceTestStage::PreMix, std::nullopt).has_value(),
		"an unknown phase completes nothing");
	harness.expect(plan.record(DeviceTestStage::PreMix, DeviceTestPhase::ChildApo) == DeviceTestStage::PreMix,
		"the driver's APO reporting completes the pre-mix stage");
	harness.expect(plan.record(DeviceTestStage::PostMix, DeviceTestPhase::Initialize) == DeviceTestStage::PostMix,
		"a stage without a chained APO completes on its own Initialize");
}

void testItemStatus(test::Harness& harness)
{
	struct Case
	{
		const char* name = nullptr;
		bool ok = false;
		bool childOk = false;
		bool useOriginal = false;
		DeviceTestItemStatus status = DeviceTestItemStatus::Error;
	};
	const Case cases[] = {
		{"an APO that never reported is an error", false, true, false, DeviceTestItemStatus::Error},
		{"even when its child APO did", false, true, true, DeviceTestItemStatus::Error},
		{"an APO that initialised is a success", true, true, false, DeviceTestItemStatus::Success},
		{"with its child APO as well, a success", true, true, true, DeviceTestItemStatus::Success},
		{"without the child APO it should chain to, a warning", true, false, true, DeviceTestItemStatus::Warning},
		{"a silent child APO nobody asked for does not matter", true, false, false, DeviceTestItemStatus::Success},
	};
	for (const Case& c : cases)
	{
		for (const DeviceTestStage stage : {DeviceTestStage::PreMix, DeviceTestStage::PostMix})
		{
			DeviceTestPlan::Result result;
			DeviceTestSelection selection = selectionOf(DeviceTestMode::SfxEfx, false);
			if (stage == DeviceTestStage::PreMix)
			{
				result.preMixOk = c.ok;
				result.childApoPreMixOk = c.childOk;
				selection.useOriginalApoPreMix = c.useOriginal;
			}
			else
			{
				result.postMixOk = c.ok;
				result.childApoPostMixOk = c.childOk;
				selection.useOriginalApoPostMix = c.useOriginal;
			}
			harness.expect(DeviceTestPlan::statusFor(result, stage, selection) == c.status,
				std::string(c.name) + (stage == DeviceTestStage::PreMix ? " (pre-mix)" : " (post-mix)"));
		}
	}

	DeviceTestSelection capture = selectionOf(DeviceTestMode::SfxEfx, false);
	capture.isInput = true;
	harness.expect(DeviceTestPlan::expectsPreMix(capture) && !DeviceTestPlan::expectsPostMix(capture),
		"a capture endpoint shows a pre-mix row only");
}

void testFallback(test::Harness& harness)
{
	// Three rounds on an automatically adjusted device that never satisfies:
	// the post-mix APO alone in SFX/EFX, the pre-mix one alone in SFX/MFX,
	// nothing in LFX/GFX. The pre-mix APO counts most, so SFX/MFX is kept.
	DeviceTestSelection selection = selectionOf(DeviceTestMode::SfxEfx, true);
	DeviceTestPlan plan(selection);

	plan.beginAttempt(selection);
	plan.record(DeviceTestStage::PostMix, DeviceTestPhase::Initialize);
	DeviceTestPlan::Fallback step = plan.fallBack(selection);
	harness.expect(!step.givesUp && step.mode == DeviceTestMode::SfxMfx, "the first failure moves down to SFX/MFX");
	harness.expect(!step.shown.preMixOk && step.shown.postMixOk, "and the rows show the round that just ran");
	harness.expect(!plan.currentResult().postMixOk, "the next attempt starts from nothing");

	selection.mode = step.mode;
	plan.beginAttempt(selection);
	plan.record(DeviceTestStage::PreMix, DeviceTestPhase::Initialize);
	step = plan.fallBack(selection);
	harness.expect(!step.givesUp && step.mode == DeviceTestMode::LfxGfx, "the second moves down to LFX/GFX");

	selection.mode = step.mode;
	plan.beginAttempt(selection);
	step = plan.fallBack(selection);
	harness.expect(step.givesUp, "with the ladder spent the device counts as not working");
	harness.expect(step.mode == DeviceTestMode::SfxMfx, "and is left in the mode that came closest");
	harness.expect(step.shown.preMixOk && !step.shown.postMixOk, "whose result the rows show");

	// Without automatic adjustment there is one attempt.
	DeviceTestSelection fixed = selectionOf(DeviceTestMode::LfxGfx, false);
	DeviceTestPlan single(fixed);
	single.beginAttempt(fixed);
	step = single.fallBack(fixed);
	harness.expect(step.givesUp && step.mode == DeviceTestMode::LfxGfx, "a fixed mode gives up after its one attempt, in that mode");
}

void testOriginalApoAfterSwitch(test::Harness& harness)
{
	struct Case
	{
		const char* name = nullptr;
		bool useOriginal = false;
		bool hadOriginal = false;
		bool hasOriginalInNewMode = false;
		bool chains = false;
	};
	const Case cases[] = {
		{"a chain the user asked for follows into a mode that has a driver APO", true, true, true, true},
		{"but not into one without", true, true, false, false},
		{"a chain the user turned off stays off", false, true, true, false},
		{"no driver APO at the start counts as no objection", false, false, true, true},
	};
	for (const Case& c : cases)
	{
		DeviceTestSelection selection = selectionOf(DeviceTestMode::SfxEfx, true);
		selection.useOriginalApoPreMix = c.useOriginal;
		selection.hasOriginalApoPreMix = c.hadOriginal;
		selection.useOriginalApoPostMix = c.useOriginal;
		selection.hasOriginalApoPostMix = c.hadOriginal;
		const DeviceTestPlan plan(selection);
		harness.expect(plan.chainsOriginalApoPreMix(c.hasOriginalInNewMode) == c.chains, std::string(c.name) + " (pre-mix)");
		harness.expect(plan.chainsOriginalApoPostMix(c.hasOriginalInNewMode) == c.chains, std::string(c.name) + " (post-mix)");
	}
}

void testWireVocabulary(test::Harness& harness)
{
	harness.expect(deviceTestStageFromWire(devicetest::wire::kStagePreMix) == DeviceTestStage::PreMix, "PreMix reads back");
	harness.expect(deviceTestStageFromWire(devicetest::wire::kStagePostMix) == DeviceTestStage::PostMix, "PostMix reads back");
	harness.expect(!deviceTestStageFromWire("premix").has_value(), "stage values are case-sensitive, as the APO writes them");
	harness.expect(deviceTestPhaseFromWire(devicetest::wire::kPhaseInitialize) == DeviceTestPhase::Initialize, "Initialize reads back");
	harness.expect(deviceTestPhaseFromWire(devicetest::wire::kPhaseChildApo) == DeviceTestPhase::ChildApo, "ChildAPO reads back");
	harness.expect(!deviceTestPhaseFromWire("").has_value(), "an empty phase is not a phase");

	// The exact bytes the APO sent before the vocabulary moved, so an APO and
	// a Device Selector from either side of the change still understand each
	// other.
	harness.expectEqual(devicetest::wire::composeMessage("{0.0.0.00000000}.{abc}", true, devicetest::wire::kPhaseInitialize),
		std::string("{\"deviceGuid\":\"{0.0.0.00000000}.{abc}\", \"stage\":\"PreMix\", \"phase\":\"Initialize\"}"),
		"the pre-mix Initialize message");
	harness.expectEqual(devicetest::wire::composeMessage("{g}", false, devicetest::wire::kPhaseChildApo),
		std::string("{\"deviceGuid\":\"{g}\", \"stage\":\"PostMix\", \"phase\":\"ChildAPO\"}"),
		"the post-mix ChildAPO message");

	harness.expect(std::string(deviceTestModeName(DeviceTestMode::LfxGfx)) == "LFX/GFX"
		&& std::string(deviceTestModeName(DeviceTestMode::SfxMfx)) == "SFX/MFX"
		&& std::string(deviceTestModeName(DeviceTestMode::SfxEfx)) == "SFX/EFX",
		"the log names each mode as before");
}
}

void runDeviceTestPlanTests(test::Harness& harness)
{
	testLadder(harness);
	testSatisfaction(harness);
	testItemStatus(harness);
	testFallback(harness);
	testOriginalApoAfterSwitch(harness);
	testWireVocabulary(harness);
}
