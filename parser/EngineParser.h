#pragma once

#include <string>

#include <mpParser.h>

// Owns the muparserx lifecycle used by the engine.  Callers can define
// engine-specific facts and evaluate expressions, but cannot partially repeat
// the package/operator bring-up sequence.
class EngineParser
{
public:
	EngineParser();

	void reinitialize();
	void beginLoad();
	void defineConst(const std::wstring& name, const mup::Value& value);
	void defineFunction(mup::ICallback* function);
	mup::Value evaluate(const std::wstring& expression);

private:
	mup::ParserX parser;
};
