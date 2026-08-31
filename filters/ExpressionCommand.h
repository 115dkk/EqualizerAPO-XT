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
#include <vector>

// Single owner of the "Eval:" config-line grammar. Evaluation itself stays
// with the engine factory, which owns the muparserx parser instance.
struct EvalCommand
{
	// Trimmed muparserx expression text.
	std::wstring expression;

	// Canonical parameter string: the expression itself.
	const std::wstring& serialize() const;

	// Returns true when command names an Eval line; expression is then filled.
	static bool parse(const std::wstring& command, const std::wstring& parameters, EvalCommand& out);
};

// Single owner of the inline `expression` grammar that ExpressionFilterFactory
// applies to every non-comment config line before the other factories see it.
// A parameter string is a sequence of literal-text and expression segments:
// backticks delimit expressions, and "\`" escapes a literal backtick on either
// side of the delimiter.
struct InlineExpression
{
	struct Segment
	{
		bool isExpression = false;

		// Literal text with escapes resolved, or the expression body.
		std::wstring text;

		bool operator==(const Segment&) const = default;
	};

	// Splits parameters into segments: "\`" yields a literal backtick (the
	// backslash is consumed); a backslash not followed by a backtick stays
	// literal; empty expressions ("``") are kept so the caller reports the
	// evaluation error; the content of an unterminated expression at the end
	// of the line is dropped.
	static std::vector<Segment> split(const std::wstring& parameters);

	// Re-creates a parameter string by re-escaping backticks in segment text.
	// For any segment list produced by split(), split(join(segments)) yields
	// the same list again. Hand-built segments round-trip too, except text
	// ending in a lone backslash directly before an expression segment, which
	// no source line can express.
	static std::wstring join(const std::vector<Segment>& segments);
};
