/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The row info a card row hands its skin, derived from facts alone: the
	row's descriptor, the selection, the lane metrics and the analysis
	engine's load trace for its line. It used to be worked out inside
	FilterCardRow (a Q_OBJECT widget), so only the offscreen gates could see
	it; as plain functions EditorLogicTests pins it (audit #422 D4).
*/

#pragma once

#include <QList>
#include <QString>

#include "Editor/widgets/CommandRowInfo.h"

struct ConfigLoadTraceEntry;
struct FilterCardDescriptor;

namespace CommandRowFacts
{
	// What the row widget knows besides its descriptor.
	struct RowContext
	{
		bool selected = false;
		bool focused = false;
		// One indent band's width (the skin's channelGroupIndent token).
		int laneUnit = 0;
		// The skin's header row height (rowHeight token).
		int rowHeight = 0;
		// ISkin::logicSiblingsIndentAsMembers of the active skin.
		bool logicSiblingsIndentAsMembers = false;
	};

	// Indent units for the card's outer margin. Branch/tail rows of the If
	// family (ElseIf/Else/EndIf) take one more unit when the skin indents
	// them with the block members.
	int indentUnits(const FilterCardDescriptor& descriptor, bool logicSiblingsIndentAsMembers);

	// The row's part in an If block (CommandRowInfo::ScopeRole), from the
	// descriptor type, the lower-cased command and the If-scope count.
	CommandRowInfo::ScopeRole scopeRole(const QString& type, const QString& command, int logicDepth);

	// Folds the analysis engine's load facts for one line into the info:
	// branch truth for the If family, computed values for Eval and inline
	// expressions, whether a false branch swallowed the line, the first
	// parse error and a setup failure. When a line has both an Eval result
	// and an inline substitution, the Eval result wins whatever the order.
	void foldLoadFacts(CommandRowInfo& info, const QList<ConfigLoadTraceEntry>& facts);

	// The whole info: identity and state from the descriptor and context,
	// lane and scope geometry, then the load facts.
	CommandRowInfo describe(const FilterCardDescriptor& descriptor, const RowContext& context,
		const QList<ConfigLoadTraceEntry>& facts);
}
