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

#include "ExpressionCommand.h"

#include "text/StringHelper.h"

using std::vector;
using std::wstring;

const wstring& EvalCommand::serialize() const
{
	return expression;
}

bool EvalCommand::parse(const wstring& command, const wstring& parameters, EvalCommand& out)
{
	if (command != L"Eval")
		return false;

	out.expression = StringHelper::trim(parameters);

	return true;
}

vector<InlineExpression::Segment> InlineExpression::split(const wstring& parameters)
{
	vector<Segment> segments;

	bool inExpression = false;
	bool lastWasBackslash = false;

	wstring output;
	wstring expression;
	for (unsigned i = 0; i < parameters.size(); i++)
	{
		wchar_t c = parameters[i];
		if (c == L'`')
		{
			if (!inExpression)
			{
				if (lastWasBackslash)
				{
					output += c;
				}
				else
				{
					inExpression = true;
					if (!output.empty())
					{
						segments.push_back({false, output});
						output.clear();
					}
				}
			}
			else
			{
				if (lastWasBackslash)
				{
					expression += c;
				}
				else
				{
					inExpression = false;
					segments.push_back({true, expression});
					expression.clear();
				}
			}
			lastWasBackslash = false;
		}
		else if (c == L'\\')
		{
			if (i >= parameters.size() - 1 || parameters[i + 1] != L'`')
			{
				if (inExpression)
					expression += c;
				else
					output += c;
			}
			lastWasBackslash = true;
		}
		else
		{
			if (inExpression)
				expression += c;
			else
				output += c;
			lastWasBackslash = false;
		}
	}

	// An unterminated expression is dropped, like the factory dropped it; only
	// pending literal text survives.
	if (!output.empty())
		segments.push_back({false, output});

	return segments;
}

wstring InlineExpression::join(const vector<Segment>& segments)
{
	wstring result;
	for (const Segment& segment : segments)
	{
		wstring escaped;
		for (wchar_t c : segment.text)
		{
			if (c == L'`')
				escaped += L"\\`";
			else
				escaped += c;
		}

		if (segment.isExpression)
			result += L"`" + escaped + L"`";
		else
			result += escaped;
	}
	return result;
}
