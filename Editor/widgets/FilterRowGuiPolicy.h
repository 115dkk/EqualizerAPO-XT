/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The row-GUI decision, as data (audit #275 B4): which editor a config line
	gets - comment card, skin routing view, modern card editor, the legacy
	factory chain, or the raw body - used to be decided inline across ninety
	widget-bound lines of FilterTable::createRowGui, so the only thing that
	could catch a policy regression was the offscreen pixel gate. Every input
	is obtainable without widgets, so the decision lives here as a pure
	function, EditorLogicTests pins it directly, and createRowGui only
	constructs what was decided.
*/

#pragma once

#include <QString>

struct FilterCardDescriptor;

enum class RowGuiDecision
{
	// Modern cards: a pure comment line gets the comment card editor.
	CommentCard,
	// Modern cards: the row hosts the active skin's routing view directly
	// (createRowGui answers no GUI on purpose).
	SkinRoutingView,
	// Modern cards: a registered card editor answers the command directly.
	CardEditor,
	// Run the legacy factory chain. In modern mode its GUI is the row's
	// editor as it stands; in legacy mode it is decorated (a construction
	// mechanic, not a decision).
	LegacyChain,
	// No "command: parameters" shape: the raw body row.
	RawRow,
};

// descriptor must be non-null when modernCards is true (it is unused in
// legacy mode). routingRendererAvailable and cardEditorAvailable are the two
// environment facts the caller resolves (active skin's renderer, card editor
// registry, asked with rowGuiCardKey's key); injecting them keeps the
// decision testable without either.
RowGuiDecision decideRowGui(bool modernCards, const QString& line,
	const FilterCardDescriptor* descriptor,
	bool routingRendererAvailable, bool cardEditorAvailable);

// What the card editor lookup is asked about a line: the key before the
// first colon (trimmed) and the parameter text after it, untouched. In card
// mode a commented command line is asked about under its uncommented key -
// one leading '#' comes off, the same strip the legacy chain's
// CommentFilterGUIFactory makes - so "# Include: a.txt" gets the decision
// "Include: a.txt" gets. In legacy mode the key stays as written.
struct RowGuiCardKey
{
	QString command;
	QString parameters;
	bool hasColon = false;
};

RowGuiCardKey rowGuiCardKey(bool modernCards, const QString& line);
