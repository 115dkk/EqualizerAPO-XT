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
	parser->SetExpr(expression);
	return parser->Eval();
}
