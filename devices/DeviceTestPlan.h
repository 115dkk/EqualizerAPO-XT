/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the device test decides for one device (audit #348 F15): which
	install modes it tries and in what order, when a device counts as working,
	which mode it falls back to when none did, and what each row's status
	shows.

	These decisions used to live inside DeviceTestThread::run(), 230 lines of
	QThread that also restarted the audio service, owned the pipe and parsed
	JSON, so nothing could test them without a real endpoint (the capture gate
	in CI does, on a machine). The thread now feeds this plan what happened and
	acts on its answers. Neither Qt nor Win32, so the suites can.

	One plan per device under test. A round of the test is: beginAttempt with
	the device's selected install state, record every message the APO sends,
	and, if the device is not satisfied when the wait runs out, fallBack to
	learn which mode to reinstall with or that the ladder is spent.
*/

#pragma once

#include <optional>
#include <string_view>
#include <vector>

class DeviceAPOInfo;

// The install modes, numbered as DeviceAPOInfo::InstallMode
// (DeviceTestPlan.cpp checks that the numbers agree).
enum class DeviceTestMode
{
	LfxGfx = 0,
	SfxMfx = 1,
	SfxEfx = 2
};

// Which of our two APOs a message is about, and how far it got.
enum class DeviceTestStage
{
	PreMix,
	PostMix
};

enum class DeviceTestPhase
{
	// The APO initialised on the endpoint.
	Initialize,
	// The driver's own APO, which ours chains to, initialised as well.
	ChildApo
};

// What a row shows for one stage.
enum class DeviceTestItemStatus
{
	Waiting,
	Success,
	Warning,
	Error
};

// The values of DeviceTestWire.h, read back. Anything else is not a stage or
// a phase, and a message carrying it changes nothing.
std::optional<DeviceTestStage> deviceTestStageFromWire(std::string_view value);
std::optional<DeviceTestPhase> deviceTestPhaseFromWire(std::string_view value);

// How the log names a mode.
const char* deviceTestModeName(DeviceTestMode mode);

// The device's selected install state, as far as the test depends on it.
// hasOriginalApo* answers for the selected mode: which of the driver's APOs
// ours would chain to depends on the slots the mode installs into.
struct DeviceTestSelection
{
	bool installPreMix = false;
	bool installPostMix = false;
	bool useOriginalApoPreMix = false;
	bool useOriginalApoPostMix = false;
	bool autoAdjust = false;
	DeviceTestMode mode = DeviceTestMode::LfxGfx;
	bool isInput = false;
	bool hasOriginalApoPreMix = false;
	bool hasOriginalApoPostMix = false;
};

DeviceTestSelection deviceTestSelectionOf(DeviceAPOInfo& info);

class DeviceTestPlan
{
public:
	// What the APO has reported during one attempt.
	struct Result
	{
		bool preMixOk = false;
		bool postMixOk = false;
		// True when there is no driver APO to wait for.
		bool childApoPreMixOk = true;
		bool childApoPostMixOk = true;

		// Which attempt came closest: the pre-mix APO counts most, the
		// post-mix one next, and a missing child APO costs a little.
		int score() const;
	};

	// What to do after an attempt that did not satisfy the device.
	struct Fallback
	{
		// The mode to reinstall with: the next one on the ladder, or the best
		// one tried when the ladder is spent.
		DeviceTestMode mode = DeviceTestMode::LfxGfx;
		// The result the rows show for this round.
		Result shown;
		// The ladder is spent: the device counts as not working and is not
		// tested again.
		bool givesUp = false;
	};

	// The ladder: with automatic adjustment every mode, SFX/EFX first and
	// LFX/GFX last; otherwise only the selected one.
	explicit DeviceTestPlan(const DeviceTestSelection& selection);

	// Starts an attempt in the selected mode: takes that mode off the ladder
	// and counts a child APO the selection does not chain to as already
	// reported. What the previous attempt heard was cleared by fallBack.
	void beginAttempt(const DeviceTestSelection& selection);

	// Records one message for a stage; a phase this build does not know
	// changes nothing but still answers. Answers the stage when its row now
	// shows success.
	std::optional<DeviceTestStage> record(DeviceTestStage stage, std::optional<DeviceTestPhase> phase);

	// Whether every stage the selection installs has reported, with its child
	// APO where it chains to one. A capture endpoint has no post-mix stage.
	bool satisfied(const DeviceTestSelection& selection) const;

	// After an attempt in selection.mode that did not satisfy the device:
	// keeps the best attempt so far, then moves down the ladder or gives up.
	Fallback fallBack(const DeviceTestSelection& selection);

	// Whether the reinstall in a new mode chains to the driver's APO: when the
	// user wanted it (or had no choice, because there was none when the test
	// started) and the new mode has one to chain to.
	bool chainsOriginalApoPreMix(bool hasOriginalApoInNewMode) const;
	bool chainsOriginalApoPostMix(bool hasOriginalApoInNewMode) const;

	const Result& currentResult() const { return current; }
	const std::vector<DeviceTestMode>& remainingModes() const { return remaining; }

	// Whether a row exists for the stage at all.
	static bool expectsPreMix(const DeviceTestSelection& selection);
	static bool expectsPostMix(const DeviceTestSelection& selection);

	// A row's status for an attempt's result: an APO that initialised is a
	// success, unless it should have chained to the driver's APO and that did
	// not initialise, which is a warning; an APO that never reported is an
	// error.
	static DeviceTestItemStatus statusFor(const Result& result, DeviceTestStage stage,
		const DeviceTestSelection& selection);

private:
	std::vector<DeviceTestMode> remaining;
	DeviceTestMode bestMode;
	Result current;
	Result best;
	bool wantsOriginalApoPreMix;
	bool wantsOriginalApoPostMix;
};
