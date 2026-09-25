/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The one way config lines read numbers (parser/NumericText.h, audit #348
	F2/TD-42), as a table: the decimal comma, what follows the number, and the
	spellings wcstod takes that a config number never uses.
*/

#include <optional>
#include <string>

#include "parser/NumericText.h"
#include "Tests/TestHarness.h"

namespace
{
test::Harness harness("NumericTextTests");

void testReadNumber()
{
	struct Case
	{
		const wchar_t* text;
		double value;
		size_t length;
		const char* what;
	};
	const Case cases[] = {
		{L"1.5", 1.5, 3, "a decimal point"},
		{L"1,5", 1.5, 3, "a decimal comma reads as a point"},
		{L"-3 dB", -3.0, 2, "the number stops before the unit"},
		{L"  12", 12.0, 4, "leading whitespace is taken"},
		{L"2.5e1x", 25.0, 5, "an exponent is decimal notation"},
		{L"0,5*L", 0.5, 3, "a Copy factor stops at the operator"},
		{L"dB", 0.0, 0, "no number"},
		{L"", 0.0, 0, "empty text"},
		{L"inf", 0.0, 0, "inf is not a config number"},
		{L"nan", 0.0, 0, "nan is not a config number"},
		{L"0x10", 0.0, 0, "hex is not a config number"},
		{L"1e999", 0.0, 0, "an overflow is not finite"},
	};
	for (const Case& c : cases)
	{
		const numeric_text::Number number = numeric_text::readNumber(c.text);
		harness.expect(number.length == c.length && number.value == c.value, std::string("readNumber: ") + c.what);
	}
}

void testParseNumber()
{
	harness.expect(numeric_text::parseNumber(L" 0,25 ") == std::optional<double>(0.25), "surrounding whitespace aside, the whole text is the number");
	harness.expect(numeric_text::parseNumber(L"-6") == std::optional<double>(-6.0), "a negative integer");
	harness.expectFalse(numeric_text::parseNumber(L"1.5dB").has_value(), "trailing text is not part of a whole number");
	harness.expectFalse(numeric_text::parseNumber(L"file.wav").has_value(), "a word is not a number");
	harness.expectFalse(numeric_text::parseNumber(L"   ").has_value(), "blank text is not a number");
	harness.expectFalse(numeric_text::parseNumber(L"inf").has_value(), "inf is refused");
}
}

void runNumericTextTests()
{
	testReadNumber();
	testParseNumber();
	harness.report();
}
