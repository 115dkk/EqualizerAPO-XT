/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#include <string>

// Single owner of the If/ElseIf/Else/EndIf config-line family grammar: which
// commands belong to the family and how the condition expression is extracted.
// The branch state machine (nesting, false-branch consumption) stays with the
// engine factory.
struct IfCommand
{
	enum class Kind
	{
		If,
		ElseIf,
		Else,
		EndIf,
	};

	Kind kind = Kind::If;

	// Trimmed condition expression. Filled for every kind (Else/EndIf lines may
	// carry text after the colon), but only If/ElseIf evaluate it.
	std::wstring expression;

	// Canonical parameter string: the expression itself.
	std::wstring serialize() const;

	// Returns true when command names a line of the If family; kind and
	// expression are then filled.
	static bool parse(const std::wstring& command, const std::wstring& parameters, IfCommand& out);
};
