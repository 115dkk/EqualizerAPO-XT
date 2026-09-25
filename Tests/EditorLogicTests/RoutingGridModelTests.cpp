/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/widgets/routing/RoutingGridModel.h"
#include "EditorLogicTestSupport.h"

void testRoutingGridModel()
{
	using Model = RoutingGridModel;
	using Adapter = CopyRoutingAdapter;
	const std::vector<std::wstring> device = { L"L", L"R", L"C" };
	std::vector<Assignment> rows = Adapter::seedTargets(Adapter::parse("VC=0.5*L+-6dB*R"), device);
	QStringList pins = { "VC" };
	const int row = Model::rowIndexOf(rows, "vc");
	requireEqual(row, 0, "row lookup is case-insensitive and keeps written order");
	expectEqual(Model::rowIndexOf(rows, "missing"), -1, "missing row lookup fails");
	expectTrue(Model::commitFactor(rows, row, "L", "0,25", true), "a comma factor commits");
	expectEqual(Adapter::serialize(rows), "VC=0.25*L+-6.0dB*R", "factor edit keeps the other summands");
	expectTrue(Model::commitFactor(rows, row, "R", " INV ", true), "INV commits on a grid");
	expectEqual(Adapter::serialize(rows), "VC=0.25*L+-1.0*R", "INV clears decibel mode");
	expectTrue(Model::commitFactor(rows, row, "C", "-3 dB", true), "factor commit creates a missing crosspoint");

	const QString before = Adapter::serialize(rows);
	int notifications = 0;
	// The view emits only when the shared commit returns true. Pin the
	// notification contract as well as the bytes, without widget dependencies.
	for (const char* invalid : { "garbage", "inf", "-inf", "nan", "1e999", "bad dB" })
	{
		if (Model::commitFactor(rows, row, "L", invalid, true))
			notifications++;
		if (Model::commitSource(rows, row, 0, QString(invalid) + "*L", false, true))
			notifications++;
		if (Model::commitChip(rows, row, 0, invalid, Model::sourceChannels(rows, device, {})))
			notifications++;
		expectEqual(Adapter::serialize(rows), before, "a rejected commit leaves the serialized state untouched");
	}
	expectEqual(notifications, 0, "rejected commits request no routingChanged");
	expectFalse(Model::commitFactor(rows, -1, "L", "1", true), "negative row cannot commit");
	expectFalse(Model::commitSource(rows, row, -1, "L", false, true), "negative summand cannot commit");
	expectFalse(Model::commitFactor(rows, row, "L", "0.5", false), "locked factors cannot commit");

	const int count = static_cast<int>(rows.size());
	expectTrue(Model::addChannel(rows, pins, " NewBus "), "a trimmed channel name commits");
	expectEqual(static_cast<int>(rows.size()), count + 1, "a new channel adds one empty row");
	expectTrue(pins.contains("NewBus"), "a new channel is pinned");
	expectTrue(Model::addChannel(rows, pins, "newbus"), "re-entering a channel is accepted");
	expectEqual(static_cast<int>(rows.size()), count + 1, "re-entry never duplicates the row");
	expectEqual(pins.size(), 2, "re-entry never duplicates its pin");
	expectEqual(Adapter::serialize(rows), before, "adding or re-entering an empty target changes no serialized routing");
	expectFalse(Model::addChannel(rows, pins, "a+b"), "an operator cannot declare a channel");
	expectFalse(Model::addChannel(rows, pins, "2"), "a positional number cannot declare a channel");
	pins.append("NEWBUS");
	Model::removePin(pins, "newbus");
	expectEqual(pins, QStringList({ "VC" }), "pin removal removes every case variant");
	expectFalse(Model::removeChannel(rows, pins, "NewBus"), "removing an empty row requests no signal");

	const QStringList sources = Model::sourceChannels(rows, device, {});
	expectTrue(Model::commitChip(rows, row, 0, "L", sources), "a chip accepts re-entering its current channel");
	expectTrue(rows[row].sourceSum[0].factor == 1.0 && rows[row].sourceSum[0].channel == L"L",
		"re-entering a chip channel resets its factor to unity like a bare source token");
	expectTrue(Model::commitChip(rows, row, 0, "R", sources), "a chip accepts another offered channel");
	expectTrue(Model::commitChip(rows, row, 0, "2", sources), "a chip retains its bare-integer gain grammar");
	expectTrue(rows[row].sourceSum[0].factor == 2.0 && rows[row].sourceSum[0].channel == L"R",
		"a chip's numeric gain does not become a positional channel");
	expectFalse(Model::commitSource(rows, row, 0, "2.0*R", false, true), "unchanged source tokens request no signal");
	expectTrue(Model::commitFactor(rows, row, "C", "", true), "an empty factor removes the crosspoint");
	expectEqual(Model::summandIndex(rows[row], "C"), -1, "the cleared crosspoint is gone");
	expectTrue(Model::removeChannel(rows, pins, "vc"), "a connected target removal requests a signal");
	expectTrue(pins.isEmpty(), "channel removal also removes its pin");

	Model model;
	Model::PortConfig config;
	model.load(Adapter::parse("VC=-6dB*L"), device, config);
	expectTrue(model.setFactorText(0, "inv"), "the port projection accepts INV too");
	expectEqual(Adapter::serialize(model.assignments()), "VC=-1.0*L", "the port projection stores inversion");
	const QString traceBefore = Adapter::serialize(model.assignments());
	for (const char* invalid : { "garbage", "inf", "nan", "1e999" })
	{
		expectFalse(model.setFactorText(0, invalid), "bad trace factors request no signal");
		expectEqual(Adapter::serialize(model.assignments()), traceBefore, "bad trace factors keep the old state");
	}
	expectTrue(model.addChannel(pins, "VC"), "the port projection accepts an existing output");
	expectTrue(model.addChannel(pins, "vc"), "the port projection accepts case-insensitive re-entry");
	expectEqual(pins.size(), 1, "the port projection re-entry does not duplicate pins");
	expectTrue(model.setFactorText(0, ""), "clearing the trace factor removes it");
	expectTrue(model.traces().isEmpty(), "the cleared trace is gone");

	// Matrix projection: first-seen source order, written target order,
	// decibels and polarity, duplicate-source last-cell display, and seeds.
	const auto matrix = Adapter::buildMatrix(Adapter::parse("R=L VC=0.5*R+-6dB*L L=0.0"));
	expectEqual(matrix.outputs, QStringList({ "R", "VC", "L" }), "matrix targets retain written order");
	expectEqual(matrix.inputs, QStringList({ "L", "R" }), "matrix sources are first-seen; constants have no column");
	expectTrue(matrix.cell(0, 0).present && matrix.cell(0, 0).factor == 1.0, "unity cell is present");
	expectFalse(matrix.cell(0, 1).present, "unconnected cells are absent");
	expectTrue(matrix.cell(1, 0).present && matrix.cell(1, 0).factor == -6.0 && matrix.cell(1, 0).isDecibel,
		"matrix retains a decibel contribution");
	expectTrue(matrix.cell(1, 1).factor == 0.5 && !matrix.cell(1, 1).isDecibel, "matrix retains a linear contribution");
	const auto duplicates = Adapter::buildMatrix(Adapter::parse("VC=0.5*L+-1.0*L"), device);
	expectEqual(duplicates.inputs, QStringList({ "L", "R", "C" }), "seed inputs precede cell indexing");
	expectTrue(duplicates.cell(0, 0).factor == -1.0, "duplicate contributions keep the historical last-cell display");
	const auto fixed = Adapter::buildMatrix(Adapter::parse("L=0.5*0+1+7 R=1"), QStringList({ "0", "1" }));
	expectEqual(fixed.inputs, QStringList({ "0", "1" }), "fixed source projection cannot grow columns");
	expectTrue(fixed.cell(0, 0).factor == 0.5 && fixed.cell(0, 1).present && fixed.cell(1, 1).present,
		"fixed source cells retain their row stride and factors");
	expectEqual(fixed.cells.size(), 3, "unknown fixed sources have no matrix cell");
	expectTrue(Adapter::buildMatrix(Adapter::parse("")).outputs.isEmpty(), "an empty Copy derives an empty matrix");
}
