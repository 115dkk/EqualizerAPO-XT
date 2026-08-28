/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Tests for the shared "Eval:" codec and the inline `expression` lexer
	(filters/ExpressionCommand.{h,cpp}), which ExpressionFilterFactory
	consumes for every non-comment config line.
*/

#include <string>
#include <vector>

#include "filters/ExpressionCommand.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
test::Harness harness("ExpressionCommandTests");

typedef InlineExpression::Segment Segment;

void testEvalCommand()
{
	EvalCommand cmd;
	harness.expectTrue(EvalCommand::parse(L"Eval", L"  x = 1  ", cmd), "'Eval' is recognized");
	harness.expectTrue(cmd.expression == L"x = 1", "Eval expression is trimmed");
	harness.expectTrue(cmd.serialize() == L"x = 1", "Eval serialization is the expression");

	harness.expectFalse(EvalCommand::parse(L"Preamp", L"x = 1", cmd), "'Preamp' must not parse as Eval");
	harness.expectFalse(EvalCommand::parse(L"eval", L"x = 1", cmd), "command match is case-sensitive");
}

void testInlineSplit()
{
	// Plain text gives one literal segment; empty text gives none.
	vector<Segment> segments = InlineExpression::split(L"plain text");
	harness.requireEqual(segments.size(), (size_t)1, "plain text segment count");
	harness.expectTrue(segments[0] == Segment{false, L"plain text"}, "plain text segment");
	harness.expectEqual(InlineExpression::split(L"").size(), (size_t)0, "empty text gives no segments");

	// Backticks delimit expressions.
	segments = InlineExpression::split(L"a `x + 1` b");
	harness.requireEqual(segments.size(), (size_t)3, "literal/expression/literal count");
	harness.expectTrue(segments[0] == Segment{false, L"a "}, "leading literal");
	harness.expectTrue(segments[1] == Segment{true, L"x + 1"}, "expression body");
	harness.expectTrue(segments[2] == Segment{false, L" b"}, "trailing literal");

	// "\`" escapes a literal backtick on both sides of the delimiter.
	segments = InlineExpression::split(L"a\\`b");
	harness.requireEqual(segments.size(), (size_t)1, "escaped backtick stays literal");
	harness.expectTrue(segments[0] == Segment{false, L"a`b"}, "escape is resolved in literal text");

	segments = InlineExpression::split(L"`a\\`b`");
	harness.requireEqual(segments.size(), (size_t)1, "escaped backtick inside expression");
	harness.expectTrue(segments[0] == Segment{true, L"a`b"}, "escape is resolved in expression text");

	// A backslash not followed by a backtick stays literal.
	segments = InlineExpression::split(L"a\\b");
	harness.requireEqual(segments.size(), (size_t)1, "plain backslash gives one literal segment");
	harness.expectTrue(segments[0] == Segment{false, L"a\\b"}, "plain backslash stays literal");

	// A double backslash before a backtick: the first backslash stays, the
	// second escapes the backtick, and the final backtick then opens an
	// unterminated (dropped) expression.
	segments = InlineExpression::split(L"\\\\`x`");
	harness.requireEqual(segments.size(), (size_t)1, "double backslash gives one literal segment");
	harness.expectTrue(segments[0] == Segment{false, L"\\`x"}, "only the second backslash escapes");

	// Empty expressions are kept so the caller reports the evaluation error.
	segments = InlineExpression::split(L"a``b");
	harness.requireEqual(segments.size(), (size_t)3, "empty expression is kept");
	harness.expectTrue(segments[1] == Segment{true, L""}, "empty expression body");

	// The content of an unterminated expression is dropped.
	segments = InlineExpression::split(L"a`bc");
	harness.requireEqual(segments.size(), (size_t)1, "unterminated expression is dropped");
	harness.expectTrue(segments[0] == Segment{false, L"a"}, "literal before unterminated expression survives");
}

void testInlineJoinRoundTrip()
{
	// split(join(split(x))) must reproduce split(x) for representative inputs.
	const wstring cases[] = {
		L"plain text",
		L"a `x + 1` b",
		L"a\\`b",
		L"`a\\`b`",
		L"a\\b",
		L"a``b",
		L"pre `one` mid `two` post",
		L"",
	};

	for (const wstring& parameters : cases)
	{
		vector<Segment> first = InlineExpression::split(parameters);
		wstring joined = InlineExpression::join(first);
		vector<Segment> second = InlineExpression::split(joined);
		harness.expectTrue(first == second, "split/join round trip is stable");
	}

	// join re-escapes backticks that split resolved.
	vector<Segment> segments = {{false, L"a`b"}, {true, L"x`y"}};
	harness.expectTrue(InlineExpression::join(segments) == L"a\\`b`x\\`y`", "join re-escapes backticks");
}
}

void runExpressionCommandTests()
{
	testEvalCommand();
	testInlineSplit();
	testInlineJoinRoundTrip();

	harness.report();
}
