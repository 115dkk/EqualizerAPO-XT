/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The frequency-response file operations both GraphicEQ editors offer:
	import, export and invert. The legacy GUI (guis/GraphicEQFilterGUI) and
	the card (GraphicEQCardEditor) carried one copy each (audit #467 TD-40);
	docs/FilterListUiPolicy.md keeps logic a legacy row shares with its card in
	one place. QtCore only, so EditorLogicTests can reach it. The dialogs and
	what each editor does with the result stay with the editors.
*/

#pragma once

#include <vector>

#include <QStringList>

#include "filters/graphicEq/GainCurveIterator.h"

class QTextStream;

namespace GraphicEQCsv
{
// The nodes in one file's text, appended to nodes in reading order. A line
// starting with '*' is a comment. A line without '.' reads ',' as the decimal
// separator. Numbers are taken in pairs (frequency, gain); an unpaired last
// number and anything that is not a number are ignored, so a malformed row
// adds nothing and reports nothing.
void appendNodes(QTextStream& text, std::vector<FilterNode>& nodes);

// The nodes of every readable file in paths, sorted by frequency. A file that
// does not open is skipped without a message.
std::vector<FilterNode> readNodes(const QStringList& paths);

// One "frequency<TAB>gain" line per node, numbers as QTextStream prints them.
void writeNodes(QTextStream& text, const std::vector<FilterNode>& nodes);

// Writes nodes to path, replacing the file; false when it does not open.
bool writeNodesToFile(const QString& path, const std::vector<FilterNode>& nodes);

// Every gain negated.
std::vector<FilterNode> inverted(std::vector<FilterNode> nodes);
}
