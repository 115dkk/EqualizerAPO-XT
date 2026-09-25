/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The channel flow of a config document, walked top-down the way the engine
	walks it: which channel names exist at each line (Copy adds to them) and
	which of them are selected (Channel replaces the selection). Before this
	unit the flow was two shared vectors handed from row widget to row widget,
	each row editing them in place, and the rule for a switched-off line was
	written in two decorators (audit #348 B6). Now the whole flow is computed
	here once, from the lines alone, and each row only reads its own element.

	No Qt on purpose: EditorLogicTests pins the flow directly.
*/

#pragma once

#include <string>
#include <vector>

// One config line as the flow reads it. command is the text before the
// first colon with any leading '#' removed (an unrecognized or lowercase key
// moves nothing, as in the engine); enabled is false for a commented-out
// line.
struct ChannelFlowLine
{
	std::wstring command;
	std::wstring parameters;
	bool enabled = true;
};

struct ChannelFlowContext
{
	std::vector<std::wstring> deviceChannels;
	// What Device: patterns are matched against, spelled the way the engine
	// builds it for the selected device (DeviceCommand::matchString); empty
	// means no device is known and every Device: line matches.
	std::wstring deviceString;
	// The stage facts the engine's Stage: rule reads (StageCommand), for the
	// device and stage the Editor analyses. The Editor's analysis engine runs
	// as the post-mix instance with a post-mix APO installed (EngineSetup's
	// defaults; AnalysisThread only sets capture), so those are the defaults
	// here too.
	bool preMix = false;
	bool capture = false;
	bool postMixInstalled = true;
};

struct ChannelFlowAtLine
{
	// The channel names in scope: the device's, plus what Copy created above.
	std::vector<std::wstring> namesInScope;
	// The channels the engine would hand a filter on this line.
	std::vector<std::wstring> selected;
};

// Element i is what line i sees, before its own effect: the same two
// vectors configureChannels/configureSelectedChannels delivered before the
// flow moved here.
std::vector<ChannelFlowAtLine> computeChannelFlow(const std::vector<ChannelFlowLine>& lines,
	const ChannelFlowContext& context);
