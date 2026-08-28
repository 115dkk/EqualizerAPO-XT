/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

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
	// Run the legacy factory chain. In modern mode the chain result is
	// followed by the comment-stripped card retry; in legacy mode it is
	// decorated. Both are construction mechanics, not decisions.
	LegacyChain,
	// No "command: parameters" shape: the raw body row.
	RawRow,
};

// descriptor must be non-null when modernCards is true (it is unused in
// legacy mode). routingRendererAvailable and cardEditorAvailable are the two
// environment facts the caller resolves (active skin's renderer, card editor
// registry); injecting them keeps the decision testable without either.
RowGuiDecision decideRowGui(bool modernCards, const QString& line,
	const FilterCardDescriptor* descriptor,
	bool routingRendererAvailable, bool cardEditorAvailable);
