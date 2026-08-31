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

#include "stdafx.h"
#include "text/WideString.h"
#include "parser/NumericText.h"
#include <cmath>
#include <regex>
#include <sstream>

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "IIRFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "IIRFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::IIR, IIRFilterFactory, L"Filter")

using std::find;
using std::regex;
using std::vector;
using std::wregex;
using std::wsmatch;
using std::wstringstream;
using std::wstring;

static wregex regexType(L"^\\s*ON\\s+([A-Za-z]+)");
static wregex regexOrder(L"\\s*Order\\s+([0-9]+)");
static wregex regexCoefficients(L"\\s+Coefficients((?: [-+0-9.eE]+)+)");

IIRFilterFactory::IIRFilterFactory()
{
}

bool IIRFilterFactory::parseCommand(const wstring& command, const wstring& parameters, IIRCommand& out,
	wstring* error)
{
	// A recognized-but-malformed IIR line either fills `error` (engine path:
	// createFilter turns it into a per-line parse report) or logs directly
	// (legacy direct callers). The distinction from "not an IIR line" is the
	// whole point: two of these branches used to return false in silence
	// (audit #275 TD-03).
	auto fail = [&](const wstring& reason) {
		if (error != nullptr)
			*error = reason;
		else
			LogFStatic(L"%s", reason.c_str());
		return false;
	};

	// starts-with check (rfind at position 0), the pre-C++20 idiom
	if (!command.starts_with(L"Filter"))
		return false;

	wsmatch match;
	if (!regex_search(parameters, match, regexType) || match.str(1) != L"IIR")
		return false;

	if (!regex_search(parameters, match, regexOrder))
		return fail(L"expected Order followed by the filter order");

	wstring orderString = match.str(1);
	unsigned order = wcstol(orderString.c_str(), nullptr, 10);
	if (order < 1)
		return fail(L"the order must be at least 1");

	if (!regex_search(parameters, match, regexCoefficients))
		return fail(L"expected Coefficients followed by the b and a coefficient lists");

	wstring coefficientsString = match.str(1);
	vector<wstring> coefficientStrings = text::split(coefficientsString, L' ');
	if (coefficientStrings.size() != (order + 1) * 2)
	{
		wstringstream reason;
		reason << L"expected " << (order + 1) * 2 << L" coefficients instead of " << coefficientStrings.size();
		return fail(reason.str());
	}

	out.order = order;
	out.coefficients.clear();
	for (const wstring& coefficientString : coefficientStrings)
	{
		double coefficient = numeric_text::parseDouble(coefficientString);
		if (!std::isfinite(coefficient))
			return fail(L"IIR coefficients must be finite");
		out.coefficients.push_back(coefficient);
	}
	if (out.coefficients[order + 1] == 0.0)
		return fail(L"the a0 coefficient must not be zero");

	return true;
}

FilterVector IIRFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IIRCommand cmd;
	wstring error;
	if (!parseCommand(command, parameters, cmd, &error))
	{
		if (!error.empty())
			return reportParseError(command, error);
		return {};
	}

	wstringstream stream;
	stream << L"Adding IIR filter of order " << cmd.order << " with coefficients";
	for (unsigned i = 0; i <= cmd.order; i++)
		stream << L" b" << i << L"=" << cmd.coefficients[i];
	for (unsigned i = 0; i <= cmd.order; i++)
		stream << L" a" << i << L"=" << cmd.coefficients[i + cmd.order + 1];

	TraceF(L"%s", stream.str().c_str());

	return singleFilter(makeFilter<IIRFilter>(cmd.coefficients));
}
