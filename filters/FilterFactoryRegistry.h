#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/IFilterFactory.h"

using FilterFactoryCreator = std::unique_ptr<IFilterFactory>(*)();

// Single source of truth for the order in which the filter factories see each
// configuration line. Factories are sorted by ascending priority (lower runs
// first) in FilterFactoryRegistry::createFactories(); the order is significant:
//
//   - Control-flow factories run first because they may blank the command to
//     skip the rest of the line (Device/If/Stage/Eval/Include).
//   - Channel selection runs before the processing filters.
//   - The processing filters run last, each matching its own command keyword.
//
// Both factories that handle "Filter ..." must keep IIR directly before
// BiQuad. Changing a value here changes the runtime order, so keep the
// numbering contiguous and intentional.
namespace FilterFactoryPriority
{
	constexpr int Device = 0;
	constexpr int If = 1;
	constexpr int Expression = 2;
	constexpr int Include = 3;
	constexpr int Stage = 4;
	constexpr int Channel = 5;
	constexpr int IIR = 6;
	constexpr int BiQuad = 7;
	constexpr int Preamp = 8;
	constexpr int Delay = 9;
	constexpr int Copy = 10;
	constexpr int Convolution = 11;
	constexpr int Hilbert = 12;
	constexpr int Velvet = 13;
	constexpr int GraphicEQ = 14;
	constexpr int VSTPlugin = 15;
	constexpr int LoudnessCorrection = 16;
}

class FilterFactoryRegistry
{
public:
	// commandKeywords: the top-level config keyword(s) this factory matches in
	// createFilter() (e.g. {L"If", L"ElseIf", L"Else", L"EndIf"}); their union is
	// knownConfigCommands().
	static bool registerFactory(int priority, FilterFactoryCreator creator,
		std::vector<std::wstring> commandKeywords);
	static std::vector<std::unique_ptr<IFilterFactory>> createFactories();

	// Canonical set of recognized top-level configuration command keywords,
	// derived from the registered factories' commandKeywords. This is the single
	// list other code consumes to tell a recognized command apart from plain text
	// / comments / unknown keys. Keys may carry a trailing token (e.g. "Filter 1"),
	// so callers match the first whitespace-delimited token of the trimmed key.
	static const std::set<std::wstring>& knownConfigCommands();

	// Resolves a configuration line's key - the text before the first colon - to
	// the command keyword the engine recognizes, or an empty string when nothing
	// claims it. This is the single place that answers "is this line a command,
	// and which one", so the engine, the Editor's card model and the card editor
	// registry cannot drift into three different answers.
	//
	// The answer is what the factories will actually do with the key, not a
	// simplification of it, so the rule has two halves. Keys beginning with
	// "Filter" resolve to "Filter" because BiQuad and IIR match by prefix, which
	// is how "Filter 1" and "Filter1" both work. Every other key must equal a
	// registered keyword exactly, because those factories compare the whole key -
	// "Channel 2" is not a Channel command and the engine never runs it.
	//
	// The match is case-sensitive, and that is load-bearing: Equalizer APO 1.4.2
	// leaves "copy: a note to self" inert precisely because "copy" is not "Copy",
	// so accepting either casing would start routing audio in configs that never
	// did.
	static std::wstring canonicalCommand(const std::wstring& key);
};

// Registers a factory with its priority and its command keyword(s) as the
// trailing varargs (at least one). knownConfigCommands() is derived from these
// registrations, so a new filter is declared in exactly one place instead of in a
// hand-maintained central list.
//
// There used to be a third parameter saying whether a recognised line that
// produces no filter is legitimate, because the engine warned about the ones that
// were not. It is gone: a factory that cannot use its own line now says so itself
// through ParseReportingFactory::reportParseError, which knows what was wrong
// instead of inferring that something was.
#define REGISTER_FILTER_FACTORY(priority, factoryType, ...) \
	namespace \
	{ \
		const bool factoryType##Registered = FilterFactoryRegistry::registerFactory(priority, []() -> std::unique_ptr<IFilterFactory> { return std::make_unique<factoryType>(); }, std::vector<std::wstring>{__VA_ARGS__}); \
	}
