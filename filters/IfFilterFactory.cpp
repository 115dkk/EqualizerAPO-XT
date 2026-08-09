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
#include "services/logging/Logging.h"
#include "parser/RegexFunctions.h"
#include "parser/RegistryFunctions.h"
#include "engine/ConfigLoadTrace.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "IfCommand.h"
#include "IfFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::If, IfFilterFactory, L"If", L"ElseIf", L"Else", L"EndIf")

using std::vector;
using std::wstring;
using namespace mup;

void IfFilterFactory::initialize(FilterEngine* engine)
{
	parser = engine->getParser();
	this->engine = engine;
}

FilterVector IfFilterFactory::startOfConfiguration()
{
	trueCount = 0;
	falseCount = 0;
	while (!trueCountStack.empty())
		trueCountStack.pop();

	return {};
}

FilterVector IfFilterFactory::startOfFile(const std::wstring& configPath)
{
	trueCountStack.push(trueCount);
	trueCount = 0;
	executeElse = false;
	if (falseCount != 0)
	{
		LogF(L"File was included inside If that evaluated to false!");
		falseCount = 0;
	}

	return {};
}

FilterVector IfFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IfCommand cmd;
	bool isIfFamily = IfCommand::parse(command, parameters, cmd);
	const wstring& expression = cmd.expression;

	// Load-trace reporting: the Editor echoes
	// branch decisions next to the config rows. Every reported line sets
	// traced so the generic skipped-line report below does not double-count
	// the line that made the branch false.
	bool traced = false;
	auto traceBranch = [&](ConfigLoadTraceEntry::Kind kind, ConfigLoadTraceEntry::Result result, bool active) {
		ConfigLoadTraceEntry entry;
		entry.kind = kind;
		entry.result = result;
		entry.active = active;
		engine->traceLoadEvent(entry);
		traced = true;
	};

	if (isIfFamily && cmd.kind == IfCommand::Kind::If)
	{
		if (falseCount == 0)
		{
			try
			{
				Value result = parser->evaluate(expression);
				bool isTrue = toBoolean(result);
				if (result.GetType() == L'b')
					TraceF(L"If(%s) evaluated to %s", expression.c_str(), result.ToString().c_str());
				else
					TraceF(L"If(%s) evaluated to %s (%s)", expression.c_str(), result.ToString().c_str(), isTrue ? L"true" : L"false");
				traceBranch(ConfigLoadTraceEntry::Kind::Condition,
					isTrue ? ConfigLoadTraceEntry::Result::True : ConfigLoadTraceEntry::Result::False, isTrue);

				if (isTrue)
				{
					trueCount++;
				}
				else
				{
					falseCount++;
					executeElse = true;
				}
			}
			catch (const ParserError& e)
			{
				LogF(L"Error while evaluating If(%s): %s", expression.c_str(), e.GetMsg().c_str());
				traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::Error, false);
				falseCount++;
			}
		}
		else
		{
			falseCount++;
		}
	}
	else if (isIfFamily && cmd.kind == IfCommand::Kind::ElseIf)
	{
		if (falseCount == 0)
		{
			if (trueCount == 0)
			{
				LogF(L"ElseIf without If!");
				traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false);
			}
			else
			{
				// The previous branch is active; the chain is satisfied and
				// this condition is never looked at.
				traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false);
				falseCount++;
				trueCount--;
			}
		}
		else if (falseCount == 1 && executeElse)
		{
			try
			{
				Value result = parser->evaluate(expression);
				bool isTrue = toBoolean(result);
				if (result.GetType() == L'b')
					TraceF(L"ElseIf(%s) evaluated to %s", expression.c_str(), result.ToString().c_str());
				else
					TraceF(L"ElseIf(%s) evaluated to %s (%s)", expression.c_str(), result.ToString().c_str(), isTrue ? L"true" : L"false");
				traceBranch(ConfigLoadTraceEntry::Kind::Condition,
					isTrue ? ConfigLoadTraceEntry::Result::True : ConfigLoadTraceEntry::Result::False, isTrue);

				if (isTrue)
				{
					falseCount--;
					trueCount++;
					executeElse = false;
				}
			}
			catch (const ParserError& e)
			{
				LogF(L"Error while evaluating ElseIf(%s): %s", expression.c_str(), e.GetMsg().c_str());
				traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::Error, false);
			}
		}
		else if (falseCount == 1)
		{
			// falseCount == 1 without executeElse: an earlier branch of this
			// chain already ran, so the condition is short-circuited.
			traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false);
		}
	}
	else if (isIfFamily && cmd.kind == IfCommand::Kind::Else)
	{
		if (falseCount == 0)
		{
			if (trueCount == 0)
			{
				LogF(L"Else without If!");
				traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false);
			}
			else
			{
				traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false);
				falseCount++;
				trueCount--;
			}
		}
		else if (falseCount == 1 && executeElse)
		{
			traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, true);
			falseCount--;
			trueCount++;
			executeElse = false;
		}
		else if (falseCount == 1)
		{
			traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false);
		}
	}
	else if (isIfFamily && cmd.kind == IfCommand::Kind::EndIf)
	{
		if (falseCount == 0)
		{
			if (trueCount == 0)
				LogF(L"EndIf without If!");
			else
				trueCount--;
		}
		else
		{
			falseCount--;
		}

		if (falseCount == 0)
			executeElse = false;
	}

	if (falseCount > 0)
	{
		// A line inside a false branch. Report it as skipped unless this very
		// line already carries a branch entry, and never report comments
		// (they would not have executed anyway).
		if (!traced && !command.empty() && command[0] != L'#')
			traceBranch(ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false);
		// skip line for further factories
		command = L"";
	}

	return {};
}

FilterVector IfFilterFactory::endOfFile(const std::wstring& configPath)
{
	if (trueCount != 0 || falseCount != 0)
	{
		LogF(L"If was not closed by EndIf!");
		falseCount = 0;
	}
	trueCount = trueCountStack.top();
	trueCountStack.pop();

	return {};
}

bool IfFilterFactory::toBoolean(const Value& value)
{
	bool result = false;

	wchar_t type = value.GetType();
	switch (type)
	{
	case 'i':
		{
			int i = static_cast<int>(value.GetInteger());
			result = (i != 0);
		}
		break;
	case 'f':
		{
			double f = value.GetFloat();
			result = (f != 0.0 && f == f);
		}
		break;
	case 'b':
		{
			result = value.GetBool();
		}
		break;
	case 's':
		{
			wstring s = value.GetString();
			result = s.length() > 0 && s != L"false" && s != L"0";
		}
		break;
	case 'm':
		{
			Matrix<Value> m = value.GetArray();
			result = m.GetRows() > 0 && m.GetCols() > 0;
		}
		break;
	default:
		result = false;
		break;
	}

	return result;
}
