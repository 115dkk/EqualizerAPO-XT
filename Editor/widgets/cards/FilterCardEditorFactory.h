/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

class IFilterGUI;
class FilterTable;
class QString;

namespace FilterCardEditorFactory
{
	// Whether a registered card editor answers this line: the canonical
	// keyword resolves to a registry entry, the inline-expression guard
	// admits it, and the entry's own predicate accepts the line (only
	// "Filter" has one: an IIR or all-pass line opens a card, an ordinary
	// biquad keeps the legacy knob GUI). Pure lookup, no construction - the
	// widget-free half that decideRowGui (FilterRowGuiPolicy) consumes (audit
	// #275 B4). Until audit #348 TD-57 it answered for the keyword alone, so
	// every "Filter: ON PK" line came back as a card and FilterTable had to
	// undo the decision.
	bool available(const QString& command, const QString& parameters);

	IFilterGUI* create(FilterTable* filterTable, const QString& command, const QString& parameters);
}
