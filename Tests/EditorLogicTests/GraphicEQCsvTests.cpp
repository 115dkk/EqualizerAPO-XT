/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include <QTextStream>

#include "Editor/widgets/cards/GraphicEQCsv.h"

// The frequency-response operations both GraphicEQ editors share (audit #467
// TD-40): the rules the two former copies held, pinned once.
TEST_CASE(EditorLogicTests, testGraphicEQCsvReadsWritesAndInverts)
{
	QString text = QStringLiteral(
		"* comment 1 2\n"
		"100\t-3.5\n"
		"20,5;1,25\n"
		"1000 2.0 7\n"
		"junk\n"
		"50 1e1\n");
	QTextStream input(&text);
	std::vector<FilterNode> nodes;
	GraphicEQCsv::appendNodes(input, nodes);
	requireTrue(nodes.size() == 4, "comment, junk and an unpaired number add nothing");
	expectNear(nodes[0].freq, 100.0, 1e-12, "first pair, frequency");
	expectNear(nodes[0].dbGain, -3.5, 1e-12, "first pair, gain");
	expectNear(nodes[1].freq, 20.5, 1e-12, "a line without '.' reads ',' as the decimal separator");
	expectNear(nodes[1].dbGain, 1.25, 1e-12, "and its gain likewise");
	expectNear(nodes[2].freq, 1000.0, 1e-12, "the pair before an unpaired number");
	expectNear(nodes[3].dbGain, 10.0, 1e-12, "exponent notation");

	QString written;
	QTextStream output(&written);
	GraphicEQCsv::writeNodes(output, { FilterNode(20.5, 1.25), FilterNode(1000, -3) });
	expectEqual(written, QStringLiteral("20.5\t1.25\n1000\t-3\n"), "one tab-separated line per node");

	const std::vector<FilterNode> flipped = GraphicEQCsv::inverted({ FilterNode(100, -3.5), FilterNode(200, 2) });
	expectNear(flipped[0].dbGain, 3.5, 1e-12, "invert negates a cut");
	expectNear(flipped[1].dbGain, -2.0, 1e-12, "invert negates a boost");
	expectNear(flipped[1].freq, 200.0, 1e-12, "invert keeps the frequency");
}
