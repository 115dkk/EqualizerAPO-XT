/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/ChannelFlow.h"

namespace
{
QStringList names(const std::vector<std::wstring>& values)
{
	QStringList result;
	for (const std::wstring& value : values)
		result.append(QString::fromStdWString(value));
	return result;
}

ChannelFlowLine line(const wchar_t* command, const wchar_t* parameters, bool enabled = true)
{
	ChannelFlowLine result;
	result.command = command;
	result.parameters = parameters;
	result.enabled = enabled;
	return result;
}

ChannelFlowContext surroundContext()
{
	ChannelFlowContext context;
	context.deviceChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
	// Spelled like the engine's match string: connection name, device
	// name, GUID.
	context.deviceString = L"Speakers Example Audio {00000000-0000-0000-0000-000000000001}";
	return context;
}
}

void testChannelFlow()
{
	const QStringList all = {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
		QStringLiteral("LFE"), QStringLiteral("RL"), QStringLiteral("RR")};
	const QStringList front = {QStringLiteral("L"), QStringLiteral("R")};

	// Each element is what its line sees before its own effect; a Channel
	// line narrows the lines below it.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Channel", L"L R"), line(L"Preamp", L"-3 dB")}, surroundContext());
		requireEqual(static_cast<int>(flow.size()), 2, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[0].selected), all, QStringLiteral("the Channel line itself sees the device selection"));
		expectEqual(names(flow[1].selected), front, QStringLiteral("the line below a Channel line sees its selection"));
		expectEqual(names(flow[1].namesInScope), all, QStringLiteral("Channel does not change the names in scope"));
	}

	// v2.41.4 (#297): a switched-off Channel line sees the flow but does not
	// narrow the lines below it, because the engine skips commented lines.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Channel", L"L R", false), line(L"Preamp", L"-3 dB")}, surroundContext());
		requireEqual(static_cast<int>(flow.size()), 2, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[1].selected), all, QStringLiteral("a switched-off Channel line narrows nothing"));
	}

	// v2.41.4 (#297): a Copy target appears in scope below the Copy line and
	// not above it; Copy never changes the selection. A switched-off Copy
	// line creates nothing.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Preamp", L"0 dB"), line(L"Copy", L"VSL=L VSR=R"), line(L"Preamp", L"0 dB"),
			 line(L"Copy", L"VX=L", false), line(L"Preamp", L"0 dB")}, surroundContext());
		requireEqual(static_cast<int>(flow.size()), 5, QStringLiteral("one flow element per line"));
		expectFalse(names(flow[0].namesInScope).contains(QStringLiteral("VSL")),
			QStringLiteral("a Copy target is not in scope above the Copy line"));
		expectFalse(names(flow[1].namesInScope).contains(QStringLiteral("VSL")),
			QStringLiteral("the Copy line itself sees the names before its own effect"));
		expectEqual(names(flow[2].namesInScope), all + QStringList{QStringLiteral("VSL"), QStringLiteral("VSR")},
			QStringLiteral("the Copy targets are in scope below the Copy line"));
		expectEqual(names(flow[2].selected), all, QStringLiteral("Copy does not change the selection"));
		expectFalse(names(flow[4].namesInScope).contains(QStringLiteral("VX")),
			QStringLiteral("a switched-off Copy line creates nothing"));
	}

	// A Channel line resolves against the names in scope, so a Copy-created
	// channel above it can be selected.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Copy", L"VSL=L"), line(L"Channel", L"VSL"), line(L"Preamp", L"0 dB")}, surroundContext());
		requireEqual(static_cast<int>(flow.size()), 3, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[2].selected), QStringList{QStringLiteral("VSL")},
			QStringLiteral("a Channel line selects a Copy-created channel above it"));
	}

	// Channel: ALL selects every name in scope, Copy-created ones included.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Channel", L"L"), line(L"Copy", L"VSL=L"), line(L"Channel", L"ALL"), line(L"Preamp", L"0 dB")},
			surroundContext());
		requireEqual(static_cast<int>(flow.size()), 4, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[2].selected), QStringList{QStringLiteral("L")},
			QStringLiteral("the selection before Channel: ALL is the narrowed one"));
		expectEqual(names(flow[3].selected), all + QStringList{QStringLiteral("VSL")},
			QStringLiteral("Channel: ALL selects every name in scope"));
	}

	// A Device: block that does not match the selected device is skipped by
	// the engine: its lines see the flow and change nothing, until a Device:
	// line that matches again.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Device", L"Headphones Other Audio"), line(L"Channel", L"L R"), line(L"Copy", L"VSL=L"),
			 line(L"Preamp", L"0 dB"), line(L"Device", L"all"), line(L"Channel", L"C"), line(L"Preamp", L"0 dB")},
			surroundContext());
		requireEqual(static_cast<int>(flow.size()), 7, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[3].selected), all, QStringLiteral("a Channel line in a non-matching Device block narrows nothing"));
		expectEqual(names(flow[3].namesInScope), all, QStringLiteral("a Copy line in a non-matching Device block creates nothing"));
		expectEqual(names(flow[6].selected), QStringList{QStringLiteral("C")},
			QStringLiteral("a matching Device line lets the lines below act again"));
	}

	// A matching Device: pattern, and no device at all, keep the flow going.
	{
		ChannelFlowContext noDevice = surroundContext();
		noDevice.deviceString.clear();
		const std::vector<ChannelFlowLine> lines = {line(L"Device", L"Headphones Other Audio"),
			line(L"Channel", L"L R"), line(L"Preamp", L"0 dB")};
		expectEqual(names(computeChannelFlow(lines, noDevice)[2].selected), front,
			QStringLiteral("without a known device every Device line matches"));
		const std::vector<ChannelFlowLine> matching = {line(L"Device", L"speakers example"),
			line(L"Channel", L"L R"), line(L"Preamp", L"0 dB")};
		expectEqual(names(computeChannelFlow(matching, surroundContext())[2].selected), front,
			QStringLiteral("a matching Device line keeps the flow going"));
	}

	// Stage: the Editor analyses the post-mix instance, so a pre-mix-only
	// block is skipped and a post-mix block is not.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Stage", L"pre-mix"), line(L"Channel", L"L R"), line(L"Preamp", L"0 dB"),
			 line(L"Stage", L"post-mix"), line(L"Channel", L"C"), line(L"Preamp", L"0 dB")},
			surroundContext());
		requireEqual(static_cast<int>(flow.size()), 6, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[2].selected), all, QStringLiteral("a Channel line in a non-matching Stage block narrows nothing"));
		expectEqual(names(flow[5].selected), QStringList{QStringLiteral("C")},
			QStringLiteral("a Channel line in the post-mix block narrows"));
	}

	// If: the Editor cannot know which branch runs. Every branch starts from
	// the state at its If line, and after EndIf the flow continues with the
	// state the If branch left.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"If", L"sampleRate > 48000"), line(L"Channel", L"L R"), line(L"Preamp", L"0 dB"),
			 line(L"ElseIf", L"sampleRate > 44100"), line(L"Channel", L"C"), line(L"Preamp", L"0 dB"),
			 line(L"Else", L""), line(L"Preamp", L"0 dB"), line(L"EndIf", L""), line(L"Preamp", L"0 dB")},
			surroundContext());
		requireEqual(static_cast<int>(flow.size()), 10, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[2].selected), front, QStringLiteral("the If branch sees its own Channel line"));
		expectEqual(names(flow[5].selected), QStringList{QStringLiteral("C")},
			QStringLiteral("an ElseIf branch sees its own Channel line"));
		expectEqual(names(flow[7].selected), all, QStringLiteral("the Else branch starts from the state at the If line"));
		expectEqual(names(flow[9].selected), front, QStringLiteral("after EndIf the flow continues from the If branch"));
	}

	// A line whose parameters carry an inline expression gets its values only
	// when the engine evaluates them; it changes nothing here. A lowercase key
	// is not a command, as in the engine.
	{
		const std::vector<ChannelFlowAtLine> flow = computeChannelFlow(
			{line(L"Channel", L"`selectedChannel`"), line(L"channel", L"L R"), line(L"Preamp", L"0 dB")},
			surroundContext());
		requireEqual(static_cast<int>(flow.size()), 3, QStringLiteral("one flow element per line"));
		expectEqual(names(flow[2].selected), all,
			QStringLiteral("dynamic and lowercase Channel lines change nothing"));
	}
}
