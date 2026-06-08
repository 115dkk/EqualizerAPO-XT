#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "IFilterFactory.h"

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
// Both factories that handle "Filter ..." (IIR before BiQuad) keep their
// historical adjacency through these constants. Changing a value here changes
// the runtime order, so keep the numbering contiguous and intentional.
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
	constexpr int GraphicEQ = 12;
	constexpr int VSTPlugin = 13;
	constexpr int LoudnessCorrection = 14;
}

class FilterFactoryRegistry
{
public:
	static bool registerFactory(int priority, FilterFactoryCreator creator);
	static std::vector<std::unique_ptr<IFilterFactory>> createFactories();

	// Canonical set of recognized top-level configuration command keywords.
	// This is the single list other code consumes to tell a recognized command
	// apart from plain text / comments / unknown keys. Keys may carry a trailing
	// token (e.g. "Filter 1"), so callers match the first whitespace-delimited
	// token of the trimmed key against this set.
	static const std::set<std::wstring>& knownConfigCommands();
};

#define REGISTER_FILTER_FACTORY(priority, factoryType) \
	namespace \
	{ \
		const bool factoryType##Registered = FilterFactoryRegistry::registerFactory(priority, []() -> std::unique_ptr<IFilterFactory> { return std::make_unique<factoryType>(); }); \
	}
