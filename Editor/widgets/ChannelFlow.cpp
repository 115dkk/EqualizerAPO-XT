/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ChannelFlow.h"

#include <algorithm>
#include <utility>

#include "audio/ChannelLayout.h"
#include "filters/ChannelCommand.h"
#include "filters/CopyCommand.h"
#include "filters/DeviceCommand.h"
#include "filters/ExpressionCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "filters/MultiConvolutionCommand.h"
#include "filters/NewChannelCommand.h"
#include "filters/StageCommand.h"
#include "text/WideString.h"

namespace
{
struct FlowState
{
	std::vector<std::wstring> names;
	std::vector<std::wstring> selected;
	// True until an explicit non-ALL Channel selection takes effect. This is
	// presentation state as well as engine state: card scopes spell ALL as no
	// badges rather than enumerating every device channel.
	bool selectionIsAll = true;
	// The engine's Stage: flag. It moves with the If branches (the If
	// factory runs before the Stage factory and blanks the lines of a branch
	// that does not run), so it is part of the branch state.
	bool stageMatches = true;
};

struct IfFrame
{
	FlowState atIf;
	FlowState ifBranchEnd;
	bool ifBranchEnded = false;
};

}

std::vector<ChannelFlowAtLine> computeChannelFlow(const std::vector<ChannelFlowLine>& lines,
	const ChannelFlowContext& context)
{
	std::vector<ChannelFlowAtLine> flow;
	flow.reserve(lines.size());

	FlowState state;
	state.names = context.deviceChannels;
	state.selected = context.deviceChannels;
	state.stageMatches = StageCommand::matchesByDefault(context.preMix, context.capture,
		context.postMixInstalled);
	// The engine's Device: flag. The Device factory runs first for every
	// line, before the If factory can blank anything, so this flag does not
	// follow the If branches.
	bool deviceMatches = true;
	std::vector<IfFrame> ifStack;

	for (const ChannelFlowLine& line : lines)
	{
		ChannelFlowAtLine atLine;
		atLine.namesInScope = state.names;
		atLine.selected = state.selected;
		atLine.selectionIsAll = state.selectionIsAll;
		atLine.indent = (state.selectionIsAll ? 0 : 1) + static_cast<int>(ifStack.size());
		atLine.logicDepth = static_cast<int>(ifStack.size());
		atLine.deviceChannelCount = context.deviceChannels.size();
		flow.push_back(std::move(atLine));
		ChannelFlowAtLine& current = flow.back();

		// A switched-off line is a comment to the engine: it sees the flow
		// (its controls keep their meaning) and changes nothing.
		if (!line.enabled)
			continue;

		const std::wstring keyword = FilterFactoryRegistry::canonicalCommand(text::trim(line.command));
		if (keyword.empty())
			continue;

		// The same order the engine's factories see a line in
		// (FilterFactoryPriority): Device, If, Stage, then Channel, Copy and
		// NewChannel.
		// A line a skipped Device: or Stage: block hides sees the flow and
		// changes nothing.
		if (keyword == L"Device")
		{
			DeviceCommand cmd;
			DeviceCommand::parse(keyword, line.parameters, cmd);
			deviceMatches = context.deviceString.empty() || cmd.matches(context.deviceString);
			continue;
		}
		if (!deviceMatches)
			continue;

		// The Editor cannot know which branch of an If block runs: that is
		// decided by expressions over the live device and the registry. The
		// flow walks every branch instead. Each branch starts from the state
		// at its If: line, and after EndIf: the flow continues with the state
		// the If: branch left, as if the first condition held. The other
		// branches still show their own view of the flow, but what they
		// change does not reach the lines after EndIf:.
		if (keyword == L"If")
		{
			ifStack.push_back({state, FlowState(), false});
			continue;
		}
		if (keyword == L"ElseIf" || keyword == L"Else")
		{
			current.indent = (state.selectionIsAll ? 0 : 1)
				+ std::max(0, static_cast<int>(ifStack.size()) - 1);
			if (!ifStack.empty())
			{
				IfFrame& frame = ifStack.back();
				if (!frame.ifBranchEnded)
				{
					frame.ifBranchEnd = state;
					frame.ifBranchEnded = true;
				}
				state = frame.atIf;
			}
			continue;
		}
		if (keyword == L"EndIf")
		{
			current.indent = (state.selectionIsAll ? 0 : 1)
				+ std::max(0, static_cast<int>(ifStack.size()) - 1);
			// An unbalanced EndIf: is ignored, as the engine ignores it.
			if (!ifStack.empty())
			{
				if (ifStack.back().ifBranchEnded)
					state = ifStack.back().ifBranchEnd;
				ifStack.pop_back();
			}
			continue;
		}

		// Include: does not change the flow here. The engine restores the
		// selection after an included file, so a Channel: inside it never
		// reaches the lines below the Include: line. The names Copy: creates
		// inside it do stay in scope for the rest of the document, and those
		// the Editor cannot see: it does not read the included file.
		if (keyword == L"Include")
			continue;

		// A line carrying an inline `expression` gets its parameters only
		// after the engine evaluates them, which the Editor cannot do; it sees
		// the flow and changes nothing (the card path shows such a line as its
		// raw text, without a Channel or Copy editor).
		if (InlineExpression::containsExpression(line.parameters))
			continue;

		if (keyword == L"Stage")
		{
			StageCommand cmd;
			StageCommand::parse(keyword, line.parameters, cmd);
			state.stageMatches = cmd.matches(context.preMix, context.capture);
			continue;
		}
		if (!state.stageMatches)
			continue;

		if (keyword == L"Channel")
		{
			// A live Channel line starts a new card group at the current If
			// depth. Hidden and dynamic Channel lines returned above and keep the
			// enclosing group, just as they keep the engine selection.
			current.indent = static_cast<int>(ifStack.size());
			ChannelCommand cmd;
			ChannelCommand::parse(keyword, line.parameters, cmd);
			state.selected = ChannelCommand::resolveSelection(cmd.channels, state.names);
			state.selectionIsAll = cmd.channels.empty()
				|| std::find(cmd.channels.begin(), cmd.channels.end(), L"ALL") != cmd.channels.end();
		}
		else if (keyword == L"Copy")
		{
			// Copy adds its targets to the names in scope and never changes
			// the selection (getSelectChannels is false in the engine).
			propagateCopyChannels(parseCopyAssignments(line.parameters), state.names);
		}
		else if (keyword == L"MultiConvolution")
		{
			// A MultiConvolution line declares its targets as channels, as
			// Copy does, so a line below it can select one; it never changes
			// the selection (audit #348 A2).
			MultiConvolutionCommand cmd;
			if (MultiConvolutionCommand::parse(keyword, line.parameters, cmd))
				cmd.declareChannels(state.names);
		}
		else if (keyword == L"NewChannel")
		{
			// NewChannel declares its names and, unlike Copy, adds them to
			// the selection (getSelectChannels is true in the engine). The
			// engine rejects the whole line when one name is unusable, and
			// then nothing changes here either.
			NewChannelCommand cmd;
			if (NewChannelCommand::parse(keyword, line.parameters, cmd) && !cmd.names.empty()
				&& NewChannelCommand::validate(cmd.names, context.deviceChannels).empty())
			{
				for (const std::wstring& name : cmd.names)
					ChannelLayout::declare(state.names, name);
				state.selected = NewChannelCommand::extendSelection(state.selected, cmd.names);
			}
		}
	}

	return flow;
}
