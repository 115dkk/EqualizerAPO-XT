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
	// Whether a registered card editor would answer this line: the canonical
	// keyword resolves to a registry entry and the inline-expression guard
	// admits it. Pure lookup, no construction - the widget-free half that
	// decideRowGui (FilterRowGuiPolicy) consumes (audit #275 B4).
	bool available(const QString& command, const QString& parameters);

	IFilterGUI* create(FilterTable* filterTable, const QString& command, const QString& parameters);
}
