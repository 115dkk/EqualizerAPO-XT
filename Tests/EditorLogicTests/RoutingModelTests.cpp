/*
	This file is part of EqualizerAPO-XT.

	Routing view models: the MultiConvolution mapping adapter, the Light
	Trace StudioRoutingModel, and the RoutingFold target-channel fold the
	Copy and MultiConvolution views share.
*/

#include <string>
#include <vector>

#include <QString>
#include <QStringList>

#include "Editor/widgets/routing/MultiConvolutionRoutingAdapter.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "Editor/widgets/routing/StudioRoutingModel.h"

#include "EditorLogicTestSupport.h"

void testMultiConvolutionRoutingAdapter()
{
	// MultiConvolutionRoutingAdapter: mappings <-> the routing views'
	// Assignment type must round-trip, because the card serializes the
	// edited view back into the config line. IR channels ride as decimal
	// summand channels carrying their factor.
	using Mapping = MultiConvolutionCommand::Mapping;
	using IrRef = MultiConvolutionCommand::IrChannelRef;

	const std::vector<Mapping> brir = {{L"L", {0, 1}}, {L"R", {2, 3}}};
	std::vector<Assignment> assignments = MultiConvolutionRoutingAdapter::toAssignments(brir, 4);
	requireEqual((int)assignments.size(), 2, "two mappings become two assignments");
	expectTrue(assignments.size() == 2
		&& assignments[0].targetChannel == L"L" && assignments[0].sourceSum.size() == 2
		&& assignments[0].sourceSum[0].channel == L"0" && assignments[0].sourceSum[1].channel == L"1"
		&& assignments[0].sourceSum[0].factor == 1.0 && !assignments[0].sourceSum[0].isDecibel,
		"IR channels become decimal summands at unity factor");

	std::vector<Mapping> roundTrip = MultiConvolutionRoutingAdapter::toMappings(assignments);
	expectTrue(roundTrip.size() == 2
		&& roundTrip[0].targetChannel == L"L" && roundTrip[0].irChannels == std::vector<IrRef>({0, 1})
		&& roundTrip[1].targetChannel == L"R" && roundTrip[1].irChannels == std::vector<IrRef>({2, 3}),
		"assignments convert back to the same mappings");

	// A factor set in the view survives the trip to mappings and back, so the
	// per-summand gain/phase editing the Copy views offer works here too.
	std::vector<Assignment> withFactor = assignments;
	withFactor[0].sourceSum[0].factor = -0.5;
	std::vector<Mapping> factored = MultiConvolutionRoutingAdapter::toMappings(withFactor);
	expectTrue(factored.size() == 2
		&& factored[0].irChannels == std::vector<IrRef>({IrRef(0, -0.5), IrRef(1)}),
		"summand factors ride into the mappings");
	std::vector<Assignment> factorBack = MultiConvolutionRoutingAdapter::toAssignments(factored, 4);
	expectTrue(factorBack.size() == 2 && factorBack[0].sourceSum.size() == 2
		&& factorBack[0].sourceSum[0].factor == -0.5 && !factorBack[0].sourceSum[0].isDecibel
		&& factorBack[0].sourceSum[1].factor == 1.0,
		"summand factors ride back into the assignments");

	// The simple form expands to every file channel for display, and to
	// nothing when the channel count is unknown (callers must not offer
	// editing then).
	std::vector<Assignment> expanded = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 3);
	expectTrue(expanded.size() == 1 && expanded[0].sourceSum.size() == 3
		&& expanded[0].sourceSum[2].channel == L"2",
		"the simple form expands to every file channel");
	std::vector<Assignment> unknown = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 0);
	expectTrue(unknown.size() == 1 && unknown[0].sourceSum.empty(),
		"an unknown channel count expands to nothing");

	// Seeded placeholder rows (empty sums) and non-numeric summands are
	// dropped on the way back, like Copy's serializer skips empty rows.
	std::vector<Assignment> edited = assignments;
	Assignment seeded;
	seeded.targetChannel = L"C";
	edited.push_back(seeded);
	Assignment::Summand bogus;
	bogus.factor = 1.0;
	bogus.channel = L"VSL";
	edited[0].sourceSum.push_back(bogus);
	std::vector<Mapping> cleaned = MultiConvolutionRoutingAdapter::toMappings(edited);
	expectTrue(cleaned.size() == 2 && cleaned[0].irChannels == std::vector<IrRef>({0, 1}),
		"placeholder rows and non-numeric summands are dropped");

	// Source ports: "0".."N-1" from the file, then any referenced index
	// beyond the file so stale connections stay visible and removable.
	QStringList ports = MultiConvolutionRoutingAdapter::sourcePorts(2, {{L"L", {0, 7}}});
	expectEqual(ports.join(','), QString("0,1,7"), "ports are the file channels plus stale references");
	QStringList portsNoFile = MultiConvolutionRoutingAdapter::sourcePorts(0, {{L"L", {2, 1}}});
	expectEqual(portsNoFile.join(','), QString("1,2"), "without a file only referenced indices appear, sorted");
}

void testStudioRoutingModel()
{
	// StudioRoutingModel: the Light Trace view's working model must seed
	// and resolve exactly like the legacy CopyFilterGUIScene (channel rows,
	// LFE/SUB alias, 1-based numeric positions, the constant input port)
	// while preserving load order, so an edit-free round trip emits the
	// assignments in the order they were written.
	auto formatted = [](const std::vector<Assignment>& list) {
		QString out;
		for (const Assignment& a : list)
		{
			if (!out.isEmpty())
				out += ' ';
			out += QString::fromStdWString(a.targetChannel) + '=';
			bool first = true;
			for (const Assignment::Summand& s : a.sourceSum)
			{
				if (!first)
					out += '+';
				first = false;
				out += QString::number(s.factor) + (s.isDecibel ? "db" : "") + '*'
					+ (s.channel.empty() ? QStringLiteral("<const>") : QString::fromStdWString(s.channel));
			}
		}
		return out;
	};
	auto summand = [](double factor, const wchar_t* channel, bool isDecibel = false) {
		Assignment::Summand s;
		s.factor = factor;
		s.isDecibel = isDecibel;
		s.channel = channel;
		return s;
	};

	const std::vector<std::wstring> surround = { L"L", L"R", L"C", L"LFE" };
	StudioRoutingModel::PortConfig copyMode;

	// Written order survives, SUB canonicalizes to the LFE chip, the
	// unknown target VC becomes a new output chip.
	std::vector<Assignment> loaded(2);
	loaded[0].targetChannel = L"VC";
	loaded[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	loaded[1].targetChannel = L"C";
	loaded[1].sourceSum = { summand(1.0, L"SUB") };

	StudioRoutingModel model;
	model.load(loaded, surround, copyMode);
	expectEqual(model.inputPorts().join(','), "L,R,C,LFE,", "inputs are the channels plus the constant port");
	expectTrue(model.constInput(model.inputPorts().size() - 1), "the last input is the constant port");
	expectEqual(model.outputPorts().join(','), "L,R,C,LFE,VC", "the unknown target joins the output row");
	expectEqual(formatted(model.assignments()), "VC=0.5*L+0.5*R C=1*LFE",
		"round trip keeps written order and canonicalizes SUB to LFE");

	// 1-based numeric positions resolve like the engine.
	std::vector<Assignment> numeric(1);
	numeric[0].targetChannel = L"3";
	numeric[0].sourceSum = { summand(1.0, L"1") };
	model.load(numeric, { L"L", L"R", L"C" }, copyMode);
	expectEqual(formatted(model.assignments()), "C=1*L", "numeric positions canonicalize to channel names");

	// Edit operations: virtual output, unity trace, factor grammar
	// (',' reads as '.', "db" suffix any case, empty removes).
	model.load({}, { L"L", L"R" }, copyMode);
	expectEqual(formatted(model.assignments()), "", "seeded chips without traces emit nothing");
	const int vx = model.addOutput("VX");
	expectTrue(vx >= 0 && model.outputPorts().last() == "VX", "a new output chip is appended");
	expectEqual(model.addOutput("L"), 0, "an existing name reuses its chip");
	model.addTrace(0, vx);
	expectEqual(formatted(model.assignments()), "VX=1*L", "a drag connects at unity gain");
	model.setFactorText(0, "0,5");
	expectEqual(formatted(model.assignments()), "VX=0.5*L", "a comma factor reads as a decimal point");
	model.setFactorText(0, "-6 dB");
	expectEqual(formatted(model.assignments()), "VX=-6db*L", "a db suffix sets decibel mode");
	model.setFactorText(0, "garbage");
	expectEqual(formatted(model.assignments()), "VX=-6db*L", "unparsable text leaves the trace unchanged");
	model.setFactorText(0, "");
	expectEqual(formatted(model.assignments()), "", "an empty commit removes the trace");

	// The constant port connects at factor 0.0 with no channel.
	model.load({}, { L"L", L"R" }, copyMode);
	model.addTrace(2, 0);
	expectEqual(formatted(model.assignments()), "L=0*<const>", "the constant port writes a value summand");

	// Fixed-source mode (MultiConvolution): the top row is exactly the
	// given port list, no constant port, factors locked to unity.
	StudioRoutingModel::PortConfig fixedMode;
	fixedMode.fixedSources = QStringList() << "0" << "1" << "2" << "3";
	fixedMode.allowFactors = false;
	std::vector<Assignment> mapped(1);
	mapped[0].targetChannel = L"L";
	mapped[0].sourceSum = { summand(1.0, L"0"), summand(1.0, L"1") };
	model.load(mapped, { L"L", L"R" }, fixedMode);
	expectEqual(model.inputPorts().join(','), "0,1,2,3", "fixed sources are the whole top row");
	expectFalse(model.constInput(model.inputPorts().size() - 1), "no constant port in fixed mode");
	model.setFactorText(0, "0.5");
	model.addTrace(2, 1);
	expectEqual(formatted(model.assignments()), "L=1*0+1*1 R=1*2", "fixed mode keeps every factor at unity");

	// Re-dragging a connected chip along its own row moves that endpoint
	// instead of silently rejecting the same-side drop. Every trace touching
	// the chip moves together, while factors and the opposite endpoints stay
	// intact.
	std::vector<Assignment> rewired(2);
	rewired[0].targetChannel = L"L";
	rewired[0].sourceSum = { summand(0.25, L"L") };
	rewired[1].targetChannel = L"R";
	rewired[1].sourceSum = { summand(0.75, L"L") };
	model.load(rewired, { L"L", L"R", L"C" }, copyMode);
	expectTrue(model.rewirePort(true, 0, 1), "a connected input chip can move to another input");
	expectEqual(formatted(model.assignments()), "L=0.25*R R=0.75*R",
		"input rewire moves every attached trace and preserves factors");
	expectTrue(model.rewirePort(false, 0, 2), "a connected output chip can move to another output");
	expectEqual(formatted(model.assignments()), "C=0.25*R R=0.75*R",
		"output rewire preserves the opposite endpoint and assignment order");
	expectFalse(model.rewirePort(true, 2, 0), "an unconnected chip has no endpoint to move");
	expectFalse(model.rewirePort(false, 1, 1), "dropping back on the same chip is a no-op");

	// removeChannel: the named channel leaves both rows with every touching
	// trace, and the surviving trace indices stay consistent.
	std::vector<Assignment> removable(2);
	removable[0].targetChannel = L"VC";
	removable[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	removable[1].targetChannel = L"R";
	removable[1].sourceSum = { summand(1.0, L"VC"), summand(1.0, L"L") };
	model.load(removable, { L"L", L"R" }, copyMode);
	expectTrue(model.removeChannel("vc"), "removing a connected channel reports a change (case-insensitive)");
	expectEqual(formatted(model.assignments()), "R=1*L", "the channel's own traces and its summand uses are gone");
	expectFalse(model.outputPorts().contains("VC"), "the output chip is gone");
	expectFalse(model.inputPorts().contains("VC"), "the input chip is gone");
	expectTrue(model.constInput(model.inputPorts().size() - 1), "the constant port survives the index shift");
	expectFalse(model.removeChannel("XX"), "removing an unknown channel reports no change");
}

void testRoutingFold()
{
	// RoutingFold: the Copy and MultiConvolution views' target-channel fold.
	// A collapsed view lists only the channels the command involves; the
	// seeded rest waits behind the reveal control.
	auto summand = [](double factor, const wchar_t* channel) {
		Assignment::Summand s;
		s.factor = factor;
		s.isDecibel = false;
		s.channel = channel;
		return s;
	};
	const std::vector<std::wstring> surround =
	{ L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" };

	// "Copy: VC=0.5*L+0.5*R R=L" over 7.1: two listed rows, six folded.
	std::vector<Assignment> parsed(2);
	parsed[0].targetChannel = L"VC";
	parsed[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	parsed[1].targetChannel = L"R";
	parsed[1].sourceSum = { summand(1.0, L"L") };
	std::vector<Assignment> seeded = parsed;
	for (const wchar_t* name : { L"L", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" })
	{
		Assignment a;
		a.targetChannel = name;
		seeded.push_back(a);
	}

	RoutingFold::Fold collapsed = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), false);
	expectEqual(collapsed.visibleRows.size(), 2, "collapsed: only the referenced targets are listed");
	expectTrue(collapsed.visibleRows.contains(0) && collapsed.visibleRows.contains(1),
		"collapsed: the listed rows are the parsed ones");
	expectEqual(collapsed.hiddenChannels, 7, "collapsed: the seeded rest is counted as hidden");
	expectEqual(collapsed.inputs.join(','), "L,R,VC", "collapsed: inputs are the referenced sums plus pins");

	RoutingFold::Fold expanded = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), true);
	expectEqual(expanded.visibleRows.size(), (int)seeded.size(), "expanded: every seeded row is listed");
	expectEqual(expanded.hiddenChannels, 0, "expanded: nothing is hidden");
	expectEqual(expanded.inputs.join(','), "L,R,VC,C,LFE,RL,RR,SL,SR",
		"expanded: referenced inputs first, then the device layout");

	// MultiConvolution uses the same target-channel fold, but its IR source
	// ports are fixed by the selected WAV and must stay present in both
	// collapsed and expanded states.
	const QStringList irSources = QStringList() << "0" << "1" << "2" << "3";
	RoutingFold::Fold fixedCollapsed = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), false, irSources);
	expectEqual(fixedCollapsed.visibleRows.size(), 2,
		"fixed-source collapsed: only mapped targets are listed");
	expectEqual(fixedCollapsed.hiddenChannels, 7,
		"fixed-source collapsed: unused targets are folded");
	expectEqual(fixedCollapsed.inputs.join(','), "0,1,2,3",
		"fixed-source collapsed: every IR port remains visible");
	RoutingFold::Fold fixedExpanded = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), true, irSources);
	expectEqual(fixedExpanded.visibleRows.size(), (int)seeded.size(),
		"fixed-source expanded: every target is listed");
	expectEqual(fixedExpanded.inputs.join(','), "0,1,2,3",
		"fixed-source expanded: source ports remain fixed");

	// An empty Copy shows the first two device channels as representatives.
	std::vector<Assignment> emptySeeded;
	for (const std::wstring& name : surround)
	{
		Assignment a;
		a.targetChannel = name;
		emptySeeded.push_back(a);
	}
	RoutingFold::Fold reps = RoutingFold::fold(emptySeeded, surround, QStringList(), false);
	expectEqual(reps.visibleRows.size(), 2, "empty: two representative rows");
	expectTrue(reps.visibleRows.contains(0) && reps.visibleRows.contains(1),
		"empty: the representatives are the first two device channels");
	expectEqual(reps.hiddenChannels, 6, "empty: the rest is hidden");
	expectEqual(reps.inputs.join(','), "L,R", "empty: representative input columns");

	// A pinned (user-added) virtual channel stays listed with an empty sum
	// and is offered as an input column - and it must not chase the
	// representatives away, or there would be nothing to route it from.
	std::vector<Assignment> pinnedSeeded = emptySeeded;
	Assignment vs;
	vs.targetChannel = L"VS";
	pinnedSeeded.push_back(vs);
	RoutingFold::Fold pinned = RoutingFold::fold(pinnedSeeded, surround,
		QStringList() << "VS", false);
	expectEqual(pinned.visibleRows.size(), 3, "pinned: the added channel joins the representatives");
	expectTrue(pinned.visibleRows.contains((int)pinnedSeeded.size() - 1),
		"pinned: the appended row is listed");
	expectEqual(pinned.inputs.join(','), "VS,L,R",
		"pinned: the added channel and the representatives are routable from");

	// Name validation: the Copy grammar's operators and pure numbers are
	// rejected, plain alphanumeric names pass.
	expectTrue(RoutingFold::isValidChannelName("VS"), "a plain name is valid");
	expectTrue(RoutingFold::isValidChannelName("XL2"), "letters and digits are valid");
	expectFalse(RoutingFold::isValidChannelName(""), "empty is rejected");
	expectFalse(RoutingFold::isValidChannelName("a b"), "whitespace is rejected");
	expectFalse(RoutingFold::isValidChannelName("a=b"), "the assignment operator is rejected");
	expectFalse(RoutingFold::isValidChannelName("a+b"), "the summand operator is rejected");
	expectFalse(RoutingFold::isValidChannelName("0.5"), "a factor-shaped token is rejected");
	expectFalse(RoutingFold::isValidChannelName("2"), "a positional number is rejected");
	expectFalse(RoutingFold::isValidChannelName("ABCDEFGHIJKLMNOPQ"), "over-long names are rejected");

	// removeChannel: the channel leaves as a target and as a summand; the
	// return value says whether the serialized line changed.
	std::vector<Assignment> removable = seeded;
	expectTrue(RoutingFold::removeChannel(removable, "vc"),
		"removing a referenced channel reports a change (case-insensitive)");
	expectEqual((int)removable.size(), (int)seeded.size() - 1, "the target row is gone");
	for (const Assignment& a : removable)
		for (const Assignment::Summand& s : a.sourceSum)
			expectTrue(QString::fromStdWString(s.channel).compare("VC", Qt::CaseInsensitive) != 0,
				"no summand references the removed channel");
	std::vector<Assignment> seedOnly = emptySeeded;
	expectFalse(RoutingFold::removeChannel(seedOnly, "SR"),
		"folding away a pure seed row is not a serialized change");
	expectEqual((int)seedOnly.size(), (int)emptySeeded.size() - 1, "the seed row itself is still removed");
}
