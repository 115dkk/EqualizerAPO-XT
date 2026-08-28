/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <memory>
#include <string>

namespace mup
{
class ICallback;
class ParserX;
class Value;
}

// Owns the muparserx lifecycle used by the engine.  Callers can define
// engine-specific facts and evaluate expressions, but cannot partially repeat
// the package/operator bring-up sequence.
class EngineParser
{
public:
	EngineParser();
	~EngineParser();

	EngineParser(const EngineParser&) = delete;
	EngineParser& operator=(const EngineParser&) = delete;

	void reinitialize();
	void beginLoad();
	void defineConst(const std::wstring& name, const mup::Value& value);
	void defineFunction(mup::ICallback* function);
	mup::Value evaluate(const std::wstring& expression);

private:
	std::unique_ptr<mup::ParserX> parser;
};
