/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Evaluation tests for the muparserx expression extensions used by the Eval
	and inline-expression config commands (parser/StringOperators.cpp,
	parser/LogicalOperators.cpp, parser/RegexFunctions.cpp).

	The test builds a ParserX and registers the extensions exactly the way
	FilterEngine::initialize() -> ExpressionFilterFactory::initialize() does for
	the live engine: clear the defaults, add the four muparserx packages, then
	define the EqualizerAPO operators/functions. The registry functions
	(readRegString / readRegDWORD) are intentionally left out because they need
	a live FilterEngine and registry state; their coverage is deferred (see the
	note at the bottom of this file).
*/

#include <string>
#include <iostream>

#include <mpParser.h>
#include "parser/EngineParser.h"
#include "Tests/TestHarness.h"

using std::wstring;
using namespace mup;

namespace
{
test::Harness harness("ParserTests");

double evalFloat(EngineParser& parser, const wstring& expr)
{
	return parser.evaluate(expr).GetFloat();
}

wstring evalString(EngineParser& parser, const wstring& expr)
{
	const Value result = parser.evaluate(expr);
	return result.GetType() == L's' ? result.GetString() : result.ToString();
}

bool evalBool(EngineParser& parser, const wstring& expr)
{
	return parser.evaluate(expr).GetBool();
}
}

void runParserTests()
{
	// Guard the whole sequence: a wrong expectation must surface as a reported
	// test failure, never as an uncaught exception that aborts the process.
	try
	{
		EngineParser parser;
		parser.reinitialize();

		// AddOperator: '+' still adds numbers, but concatenates when either side is
		// a string.
		harness.expectTrue(evalFloat(parser, L"2 + 3") == 5.0, "overridden '+' must still add numbers");
		harness.expectTrue(evalString(parser, L"\"foo\" + \"bar\"") == L"foobar", "'+' should concatenate two strings");
		harness.expectTrue(evalString(parser, L"\"x=\" + 5") == L"x=5", "'+' should concatenate a string and a number");

		// NotOperator: infix logical not. The operator calls GetBool() on its
		// operand and this muparserx fork has no implicit int->bool conversion, so
		// the operand must already be boolean (a comparison), mirroring how the
		// engine's If: conditions are written and read (IfFilterFactory GetBool()).
		harness.expectTrue(evalBool(parser, L"not (1 == 0)") == true, "not of a false comparison is true");
		harness.expectTrue(evalBool(parser, L"not (1 == 1)") == false, "not of a true comparison is false");

		// regexSearch: returns an array (whole match + capture groups) on a hit and
		// an empty array on a miss. sizeof() reports the array length.
		harness.expectTrue(evalFloat(parser, L"sizeof(regexSearch(\"a(b+)c\", \"xxabbbcxx\"))") == 2.0,
			"regexSearch should return whole match plus one capture group");
		harness.expectTrue(evalFloat(parser, L"sizeof(regexSearch(\"zzz\", \"xxabbbcxx\"))") == 0.0,
			"regexSearch with no match should return an empty array");

		// regexReplace: replaces every match and returns the result string.
		harness.expectTrue(evalString(parser, L"regexReplace(\"a\", \"banana\", \"o\")") == L"bonono",
			"regexReplace should replace all matches");

		// Type-error path: regexSearch requires string arguments and must throw a
		// ParserError when given a number.
		bool threw = false;
		try
		{
			parser.evaluate(L"regexSearch(1, 2)");
		}
		catch (const ParserError&)
		{
			threw = true;
		}
		harness.expectTrue(threw, "regexSearch with non-string arguments should raise a ParserError");
	}
	catch (const ParserError& e)
	{
		std::wcerr << L"ParserTests: unexpected ParserError: " << e.GetMsg() << std::endl;
		harness.expectTrue(false, "unexpected ParserError during parser tests (see stderr)");
	}
	catch (const std::exception& e)
	{
		std::cerr << "ParserTests: unexpected exception: " << e.what() << std::endl;
		harness.expectTrue(false, "unexpected std::exception during parser tests (see stderr)");
	}

	harness.report();
}

// Deferred: readRegString / readRegDWORD (parser/RegistryFunctions.cpp) are not
// covered here. They are constructed with a FilterEngine* and read real
// registry values, so testing them needs either a live engine or a writable
// registry key. That setup is out of scope for this pure-evaluation test.
