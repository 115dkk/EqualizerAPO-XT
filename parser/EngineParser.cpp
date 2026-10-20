/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "EngineParser.h"

#include <mpParser.h>
#include <mpPackageCommon.h>
#include <mpPackageMatrix.h>
#include <mpPackageNonCmplx.h>
#include <mpPackageStr.h>

#include "ParserExtensions.h"

EngineParser::EngineParser()
	: parser(std::make_unique<mup::ParserX>())
{
	parser->EnableAutoCreateVar(true);
}

EngineParser::~EngineParser() = default;

void EngineParser::reinitialize()
{
	parser->ClearConst();
	parser->ClearFun();
	parser->ClearInfixOprt();
	parser->ClearOprt();
	parser->ClearPostfixOprt();
	parser->AddPackage(mup::PackageCommon::Instance());
	parser->AddPackage(mup::PackageNonCmplx::Instance());
	parser->AddPackage(mup::PackageStr::Instance());
	parser->AddPackage(mup::PackageMatrix::Instance());
	registerEngineFreeParserExtensions(*parser);
}

void EngineParser::beginLoad()
{
	parser->ClearVar();
}

void EngineParser::defineConst(const std::wstring& name, const mup::Value& value)
{
	parser->DefineConst(name, value);
}

void EngineParser::defineFunction(mup::ICallback* function)
{
	parser->DefineFun(function);
}

mup::Value EngineParser::evaluate(const std::wstring& expression)
{
	if (expression.find(L';') == std::wstring::npos)
	{
		parser->SetExpr(expression);
		return parser->Eval();
	}

	// Equalizer APO 1.4.2's muparserx 3.0.1 treated ';' as a newline.
	// Keep one whole-expression parse/evaluation: splitting and evaluating
	// statements separately would execute assignments before a later syntax
	// error. Equal-length replacements also preserve diagnostic positions.
	std::wstring normalized = expression;
	bool inString = false;
	bool inComment = false;
	bool escaped = false;
	bool replaced = false;
	for (wchar_t& character : normalized)
	{
		if (inComment)
		{
			if (character == L'\n')
				inComment = false;
			continue;
		}
		if (inString)
		{
			if (escaped)
				escaped = false;
			else if (character == L'\\')
				escaped = true;
			else if (character == L'"')
				inString = false;
			continue;
		}
		if (character == L'"')
			inString = true;
		else if (character == L'#')
			inComment = true;
		else if (character == L';')
		{
			character = L'\n';
			replaced = true;
		}
	}

	// The old lexer skipped spaces and accepted empty newline statements.
	// Preserve that void result only when this conversion made the script
	// empty. SetExpr on the original still enforces today's length limit.
	if (replaced && normalized.find_first_not_of(L" \n") == std::wstring::npos)
	{
		parser->SetExpr(expression);
		return mup::Value();
	}
	parser->SetExpr(replaced ? normalized : expression);
	return parser->Eval();
}
