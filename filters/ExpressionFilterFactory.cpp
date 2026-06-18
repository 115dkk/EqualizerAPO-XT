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

#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"
#include "parser/RegexFunctions.h"
#include "parser/RegistryFunctions.h"
#include "parser/StringOperators.h"
#include "parser/LogicalOperators.h"
#include "FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "ExpressionCommand.h"
#include "ExpressionFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Expression, ExpressionFilterFactory, true, L"Eval")

using std::vector;
using std::wstring;
using namespace mup;

void ExpressionFilterFactory::initialize(FilterEngine* engine)
{
	parser = engine->getParser();
	parser->DefineConst(L"inputChannelCount", mup::int_type(engine->getInputChannelCount()));
	parser->DefineConst(L"outputChannelCount", mup::int_type(engine->getOutputChannelCount()));
	parser->DefineConst(L"sampleRate", mup::float_type(engine->getSampleRate()));

	parser->DefineFun(new ReadRegStringFunction(engine));
	parser->DefineFun(new ReadRegDWORDFunction(engine));
	parser->DefineFun(new RegexSearchFunction());
	parser->DefineFun(new RegexReplaceFunction());

	parser->RemoveOprt(L"+");
	parser->DefineOprt(new AddOperator());
	parser->DefineInfixOprt(new NotOperator());
}

vector<IFilter*> ExpressionFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command.length() > 0 && command[0] == L'#')
		return vector<IFilter*>();

	// Lex through the shared codec, then evaluate the expression segments in
	// place so the other factories see the substituted parameter text.
	wstring output;
	for (const InlineExpression::Segment& segment : InlineExpression::split(parameters))
	{
		if (!segment.isExpression)
		{
			output += segment.text;
			continue;
		}

		try
		{
			parser->SetExpr(segment.text);
			Value result = parser->Eval();
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
		}
	}

	parameters = output;

	EvalCommand evalCmd;
	if (EvalCommand::parse(command, parameters, evalCmd))
	{
		try
		{
			parser->SetExpr(evalCmd.expression);
			Value result = parser->Eval();
			wstring resultString;
			if (result.GetType() == L's')
				resultString = result.GetString();
			else
				resultString = result.ToString().c_str();
			TraceF(L"Expression %s evaluated to %s", evalCmd.expression.c_str(), resultString.c_str());
		}
		catch (const ParserError& e)
		{
			LogF(L"Error while evaluating expression %s: %s", evalCmd.expression.c_str(), e.GetMsg().c_str());
		}

		// command has been handled
		command = L"";
	}

	return vector<IFilter*>();
}
