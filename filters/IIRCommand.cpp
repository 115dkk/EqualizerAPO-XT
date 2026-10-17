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

#include "IIRCommand.h"

#include <cmath>
#include <cstdio>
#include <regex>
#include <sstream>

#include "parser/NumericText.h"
#include "text/WideString.h"

using std::vector;
using std::wregex;
using std::wsmatch;
using std::wstringstream;
using std::wstring;

bool IIRCommand::parse(const wstring& command, const wstring& parameters,
	IIRCommand& out, wstring& error)
{
	error.clear();
	if (!command.starts_with(L"Filter"))
		return false;

	static const wregex regexType(L"^\\s*ON\\s+([A-Za-z]+)");
	static const wregex regexOrder(L"\\s*Order\\s+([0-9]+)");
	static const wregex regexCoefficients(L"\\s+Coefficients((?: [-+0-9.eE]+)+)");

	wsmatch match;
	if (!regex_search(parameters, match, regexType) || match.str(1) != L"IIR")
		return false;

	auto fail = [&error](const wstring& reason) {
		error = reason;
		return false;
	};

	if (!regex_search(parameters, match, regexOrder))
		return fail(L"expected Order followed by the filter order");

	const wstring orderString = match.str(1);
	const unsigned order = wcstol(orderString.c_str(), nullptr, 10);
	if (order < 1)
		return fail(L"the order must be at least 1");

	if (!regex_search(parameters, match, regexCoefficients))
		return fail(L"expected Coefficients followed by the b and a coefficient lists");

	const wstring coefficientsString = match.str(1);
	const vector<wstring> coefficientStrings = text::split(coefficientsString, L' ');
	if (coefficientStrings.size() != (order + 1) * 2)
	{
		wstringstream reason;
		reason << L"expected " << (order + 1) * 2 << L" coefficients instead of "
			<< coefficientStrings.size();
		return fail(reason.str());
	}

	out.order = order;
	out.coefficients.clear();
	for (const wstring& coefficientString : coefficientStrings)
	{
		const double coefficient = numeric_text::parseDouble(coefficientString);
		if (!std::isfinite(coefficient))
			return fail(L"IIR coefficients must be finite");
		out.coefficients.push_back(coefficient);
	}
	if (out.coefficients[order + 1] == 0.0)
		return fail(L"the a0 coefficient must not be zero");

	return true;
}

bool IIRCommand::parse(const wstring& command, const wstring& parameters,
	IIRCommand& out)
{
	wstring ignoredError;
	return parse(command, parameters, out, ignoredError);
}

wstring IIRCommand::serialize() const
{
	wstring result = L"ON IIR Order " + std::to_wstring(order) + L" Coefficients";

	wchar_t buffer[32];
	for (double coefficient : coefficients)
	{
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L" %g", coefficient);
		result += buffer;
	}

	return result;
}
