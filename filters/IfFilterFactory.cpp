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
#include "platform/windows/TextEncoding.h"
#include "engine/ConfigLoadTrace.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "IfCommand.h"
#include "IfFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::If, IfFilterFactory, L"If", L"ElseIf", L"Else", L"EndIf")

using std::vector;
using std::wstring;
using namespace mup;

void IfFilterFactory::onInitialize(FilterEngine& engine)
{
	parser = engine.getParser();
}

LineOutcome IfFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	IfCommand cmd;
	if (!IfCommand::parse(command, parameters, cmd))
		return LineOutcome::notMine();

	const wstring& expression = cmd.expression;
	wstring malformedReason;
	auto traceBranch = [&](ConfigLoadTraceEntry::Kind kind,
		ConfigLoadTraceEntry::Result result, bool active) {
		ConfigLoadTraceEntry entry;
		entry.kind = kind;
		entry.result = result;
		entry.active = active;
		engine()->traceLoadEvent(entry);
	};
	auto traceSkipped = [&] {
		ConfigLoadTraceEntry entry;
		entry.kind = ConfigLoadTraceEntry::Kind::SkippedLine;
		engine()->traceLoadEvent(entry);
	};
	auto evaluate = [&](const wchar_t* commandName, bool& value) {
		try
		{
			Value result = parser->evaluate(expression);
			value = toBoolean(result);
			if (result.GetType() == L'b')
				TraceF(L"%s(%s) evaluated to %s", commandName, expression.c_str(), result.ToString().c_str());
			else
				TraceF(L"%s(%s) evaluated to %s (%s)", commandName, expression.c_str(),
					result.ToString().c_str(), value ? L"true" : L"false");
			traceBranch(ConfigLoadTraceEntry::Kind::Condition,
				value ? ConfigLoadTraceEntry::Result::True : ConfigLoadTraceEntry::Result::False, value);
			return true;
		}
		catch (const ParserError& e)
		{
			LogF(L"Error while evaluating %s(%s): %s", commandName, expression.c_str(), e.GetMsg().c_str());
			malformedReason = L"condition could not be evaluated: " + e.GetMsg();
		}
		catch (const std::exception& e)
		{
			const wstring reason = wintext::toWideString(e.what(), CP_UTF8);
			LogF(L"Error while evaluating %s(%s): %s", commandName, expression.c_str(), reason.c_str());
			malformedReason = L"condition could not be evaluated: " + reason;
		}
		catch (...)
		{
			LogF(L"Error while evaluating %s(%s): unknown exception", commandName, expression.c_str());
			malformedReason = L"condition could not be evaluated: unknown exception";
		}
		value = false;
		traceBranch(ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::Error, false);
		return false;
	};

	if (cmd.kind == IfCommand::Kind::If)
	{
		if (!engine()->ifGatesOpen())
		{
			engine()->pushIfGate(false, false);
			traceSkipped();
		}
		else
		{
			bool open = false;
			evaluate(L"If", open);
			engine()->pushIfGate(open, open);
		}
	}
	else if (cmd.kind == IfCommand::Kind::ElseIf)
	{
		if (!engine()->hasIfGate())
		{
			malformedReason = L"ElseIf without an If before it";
			traceBranch(ConfigLoadTraceEntry::Kind::Condition,
				ConfigLoadTraceEntry::Result::NotEvaluated, false);
		}
		else if (!engine()->ifGateParentOpen())
		{
			traceSkipped();
		}
		else if (engine()->ifBranchTaken())
		{
			engine()->setGate(ConfigGateKind::If, false);
			traceBranch(ConfigLoadTraceEntry::Kind::Condition,
				ConfigLoadTraceEntry::Result::NotEvaluated, false);
		}
		else
		{
			bool open = false;
			evaluate(L"ElseIf", open);
			engine()->setGate(ConfigGateKind::If, open);
			if (open)
				engine()->setIfBranchTaken(true);
		}
	}
	else if (cmd.kind == IfCommand::Kind::Else)
	{
		if (!engine()->hasIfGate())
		{
			malformedReason = L"Else without an If before it";
			traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch,
				ConfigLoadTraceEntry::Result::NotEvaluated, false);
		}
		else if (!engine()->ifGateParentOpen())
		{
			traceSkipped();
		}
		else
		{
			const bool open = !engine()->ifBranchTaken();
			engine()->setGate(ConfigGateKind::If, open);
			if (open)
				engine()->setIfBranchTaken(true);
			traceBranch(ConfigLoadTraceEntry::Kind::ElseBranch,
				ConfigLoadTraceEntry::Result::NotEvaluated, open);
		}
	}
	else
	{
		if (!engine()->hasIfGate())
		{
			malformedReason = L"EndIf without an If before it";
		}
		else
		{
			const bool skipped = !engine()->ifGateParentOpen();
			engine()->popIfGate();
			if (skipped)
				traceSkipped();
		}
	}

	if (!malformedReason.empty())
		return LineOutcome::malformed(std::move(malformedReason));
	return LineOutcome::consumed();
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
