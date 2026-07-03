/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <QString>

class FilterTable;
class IFilterGUI;

using FilterCardEditorCreator = IFilterGUI* (*)(FilterTable* filterTable, const QString& command, const QString& parameters);

// Self-registration for the modern card editors, mirroring the engine's
// REGISTER_FILTER_FACTORY and the Editor's REGISTER_FILTER_GUI_FACTORY: each
// card editor's .cpp is the single place it joins the roster, so adding a card
// no longer means editing a hand-maintained if-chain in
// FilterCardEditorFactory. Commands are keyed lowercase; unlike the two
// factory registries there is no ordering, because card commands are exact
// keyword matches. (audit #146 TD003)
class FilterCardEditorRegistry
{
public:
	static bool registerEditor(const QString& lowercaseCommand, FilterCardEditorCreator creator);

	// The creator registered for the lowercased command keyword, or nullptr
	// when no card editor covers it.
	static FilterCardEditorCreator find(const QString& normalizedCommand);
};

// Registers a card editor for a (lowercase identifier) command keyword. The
// editor's .cpp is compiled straight into the Editor executable, so its static
// initializer always runs and no /WHOLEARCHIVE is needed.
#define REGISTER_FILTER_CARD_EDITOR(keyword, ...) \
	namespace \
	{ \
		const bool keyword##CardEditorRegistered = FilterCardEditorRegistry::registerEditor(QStringLiteral(#keyword), __VA_ARGS__); \
	}
