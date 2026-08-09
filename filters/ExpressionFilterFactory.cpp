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
#include <sstream>

#include "services/logging/Logging.h"
#include "parser/ParserExtensions.h"
#include "parser/RegistryFunctions.h"
#include "engine/ConfigLoadTrace.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "ExpressionCommand.h"
#include "ExpressionFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Expression, ExpressionFilterFactory, L"Eval")

using std::vector;
using std::wstring;
using namespace mup;

void ExpressionFilterFactory::initialize(FilterEngine* engine)
{
	parser = engine->getParser();
	this->engine = engine;
	parser->defineConst(L"inputChannelCount", mup::int_type(engine->getInputChannelCount()));
	parser->defineConst(L"outputChannelCount", mup::int_type(engine->getOutputChannelCount()));
	parser->defineConst(L"sampleRate", mup::float_type(engine->getSampleRate()));

	parser->defineFunction(new ReadRegStringFunction(engine));
	parser->defineFunction(new ReadRegDWORDFunction(engine));
}

FilterVector ExpressionFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command.length() > 0 && command[0] == L'#')
		return {};

	// Lex through the shared codec, then evaluate the expression segments in
	// place so the other factories see the substituted parameter text.
	wstring output;
	bool hadInlineExpression = false;
	bool inlineError = false;
	for (const InlineExpression::Segment& segment : InlineExpression::split(parameters))
	{
		if (!segment.isExpression)
		{
			output += segment.text;
			continue;
		}

		hadInlineExpression = true;
		try
		{
			Value result = parser->evaluate(segment.text);
			wstring resultString;
			if (result.GetType() == L's')
				resultString = result.GetString();
			else
				resultString = result.ToString().c_str();
			output += resultString;
			TraceF(L"Inline expression %s evaluated to %s", segment.text.c_str(), resultString.c_str());
		}
		catch (const ParserError& e)
		{
			LogF(L"Error while evaluating inline expression %s: %s", segment.text.c_str(), e.GetMsg().c_str());
			inlineError = true;
		}
	}

	parameters = output;

	if (hadInlineExpression)
	{
		// Load-trace: the Editor shows what a
		// line's `expression` segments resolved to on this load.
		ConfigLoadTraceEntry entry;
		entry.kind = ConfigLoadTraceEntry::Kind::InlineValue;
		entry.text = output;
		entry.error = inlineError;
		engine->traceLoadEvent(std::move(entry));
	}

	EvalCommand evalCmd;
	if (EvalCommand::parse(command, parameters, evalCmd))
	{
		ConfigLoadTraceEntry entry;
		entry.kind = ConfigLoadTraceEntry::Kind::Eval;
		try
		{
			Value result = parser->evaluate(evalCmd.expression);
			wstring resultString;
			if (result.GetType() == L's')
				resultString = result.GetString();
			else
				resultString = result.ToString().c_str();
			TraceF(L"Expression %s evaluated to %s", evalCmd.expression.c_str(), resultString.c_str());
			entry.text = resultString;
		}
		catch (const ParserError& e)
		{
			LogF(L"Error while evaluating expression %s: %s", evalCmd.expression.c_str(), e.GetMsg().c_str());
			entry.text = e.GetMsg();
			entry.error = true;
		}
		engine->traceLoadEvent(std::move(entry));

		// command has been handled
		command = L"";
	}

	return {};
}
