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
#include "../TestHarness.h"

using std::wstring;
using namespace mup;

namespace
{
test::HarnessAccess harness;

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

TEST_CASE(ParserTests, runParserTests)
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
}

TEST_CASE(ParserTests, testLegacyStatementSeparators)
{
	try
	{
		EngineParser parser;
		parser.reinitialize();
		harness.expectNear(evalFloat(parser, L"inL=1;inR=2"), 2.0, 0.0,
			"legacy Eval assignments return the last statement");
		harness.expectNear(evalFloat(parser, L"inL"), 1.0, 0.0, "first assignment is retained");
		harness.expectNear(evalFloat(parser, L"inR"), 2.0, 0.0, "second assignment is retained");
		harness.expectNear(evalFloat(parser, L";;inL=3; ;inR=4;;"), 4.0, 0.0,
			"leading, trailing and empty statements preserve legacy syntax");
		harness.expectNear(evalFloat(parser, L"inL=5\ninR=6"), 6.0, 0.0,
			"existing newline-separated expressions remain valid");
		harness.expectTrue(evalString(parser, LR"(text="a;b";n=1;text)") == L"a;b",
			"semicolons inside a string are preserved");
		harness.expectTrue(evalString(parser, LR"(text="a\";b";n=2;text)") == L"a\";b",
			"an escaped quote does not end the string");
		harness.expectTrue(evalString(parser, LR"(text="a\\;b";n=3;text)") == L"a\\;b",
			"escaped backslashes do not change string boundaries");
		harness.expectTrue(evalString(parser, LR"("; ;")") == L"; ;",
			"a string containing only separators is still a string");
		harness.expectNear(evalFloat(parser, L"values={1,2};values[1]"), 2.0, 0.0,
			"an array followed by a statement keeps array syntax");
		harness.expectTrue(parser.evaluate(L"; ;\n").GetType() == L'v',
			"a legacy separator-only expression returns void");
		harness.expectNear(evalFloat(parser, L"inL"), 5.0, 0.0,
			"a separator-only expression changes no variable");

		parser.evaluate(L"a=10");
		parser.evaluate(L"b=20");
		harness.expectNear(evalFloat(parser, L"a=1;# ; b=2"), 1.0, 0.0,
			"semicolons inside a comment do not start another statement");
		harness.expectNear(evalFloat(parser, L"b"), 20.0, 0.0, "commented assignments do not run");
		harness.expectNear(evalFloat(parser, L"a=1;# \" ; b=2\nb=3"), 3.0, 0.0,
			"a quote inside a comment does not hide the next line");
		harness.expectNear(evalFloat(parser, L"a=1;# ; b=2\rb=4"), 1.0, 0.0,
			"a carriage return alone does not end a comment");
		harness.expectNear(evalFloat(parser, L"b"), 3.0, 0.0, "the carriage-return comment stays inactive");
		harness.expectTrue(evalString(parser, LR"(text="#;";a=1;text)") == L"#;",
			"hash and semicolon inside a string remain literal text");

		for (const wchar_t* expression : {L"", L" \t\r\n", L";\t;", L";\r;"})
		{
			bool threw = false;
			try
			{
				parser.evaluate(expression);
			}
			catch (const ParserError&)
			{
				threw = true;
			}
			harness.expectTrue(threw, "empty expressions and unsupported whitespace keep their error");
		}
		bool tooLong = false;
		try
		{
			parser.evaluate(wstring(10000, L';'));
		}
		catch (const ParserError&)
		{
			tooLong = true;
		}
		harness.expectTrue(tooLong, "separator-only expressions retain the parser's length limit");
	}
	catch (const ParserError& error)
	{
		std::wcerr << L"Legacy separator evaluation: " << error.GetMsg() << std::endl;
		harness.expectTrue(false, "legacy separator evaluation must succeed");
	}
}

TEST_CASE(ParserTests, testLegacySeparatorsPreserveWholeExpressionErrors)
{
	EngineParser parser;
	parser.reinitialize();
	parser.evaluate(L"a=10");
	parser.evaluate(L"b=20");
	int separatorErrorPosition = -1;
	int newlineErrorPosition = -1;
	for (int variant = 0; variant < 2; ++variant)
	{
		bool threw = false;
		try
		{
			parser.evaluate(variant == 0 ? L"a=1;b=(" : L"a=1\nb=(");
		}
		catch (const ParserError& error)
		{
			threw = true;
			(variant == 0 ? separatorErrorPosition : newlineErrorPosition) = error.GetPos();
		}
		harness.expectTrue(threw, "a later syntax error is reported");
		harness.expectNear(evalFloat(parser, L"a"), 10.0, 0.0,
			"a later syntax error must not execute an earlier assignment");
		harness.expectNear(evalFloat(parser, L"b"), 20.0, 0.0,
			"a malformed assignment leaves its variable unchanged");
	}
	harness.expectEqual(separatorErrorPosition, newlineErrorPosition,
		"separator normalization preserves diagnostic offsets");

	for (const wchar_t* expression : {
		L"(a=1;b=2)", L"max(a=1;b=2,3)", L"{1,2;3,4}", LR"(text="unterminated;a=1)"})
	{
		bool threw = false;
		try
		{
			parser.evaluate(expression);
		}
		catch (const ParserError&)
		{
			threw = true;
		}
		harness.expectTrue(threw, "invalid nested separators and unterminated strings remain errors");
		harness.expectNear(evalFloat(parser, L"a"), 10.0, 0.0,
			"invalid nested syntax does not execute assignments");
	}

	bool runtimeError = false;
	try
	{
		parser.evaluate(L"a=1;b={1,2}[9]");
	}
	catch (const ParserError&)
	{
		runtimeError = true;
	}
	harness.expectTrue(runtimeError, "a later runtime error is still reported");
	harness.expectNear(evalFloat(parser, L"a"), 1.0, 0.0,
		"runtime errors retain the parser's already-executed assignments");
	harness.expectNear(evalFloat(parser, L"b"), 20.0, 0.0,
		"the failing runtime assignment does not replace its value");
}

// Deferred: readRegString / readRegDWORD (parser/RegistryFunctions.cpp) are not
// covered here. They are constructed with a FilterEngine* and read real
// registry values, so testing them needs either a live engine or a writable
// registry key. That setup is out of scope for this pure-evaluation test.
