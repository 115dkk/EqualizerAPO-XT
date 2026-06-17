/*
	This file is part of EqualizerAPO-XT.

	Round-trip tests for the shared VSTPlugin config-line parser/serializer
	(VSTPluginCommand::parse + VSTPluginCommand::serialize in
	filters/VSTPluginCommand.cpp). They confirm that representative "VSTPlugin:"
	lines - library only, library + ChunkData, library + named params, library +
	id params, and quoted paths/names - parse into the same library/chunkData/
	paramMap the engine factory used to read back off a throwaway VSTPluginFilter,
	and that serializing the parsed command reproduces the canonical parameter
	body so serialize(parse(line)) round-trips.

	Parsing only resolves the cached VSTPluginLibrary object (getInstance does not
	load the binary), so these tests need no real plugin. To stay independent of
	the install registry key - getDefaultPluginPath() reads it and throws when it
	is missing - every test uses an absolute library path, which the parser keeps
	verbatim instead of resolving against the default plugin directory.

	These tests link against the same Common.lib as HybridConvTests and run from
	its main() via runVSTPluginCommandTests().
*/

#include <string>
#include <unordered_map>

#include "filters/VSTPluginCommand.h"
#include "Tests/TestHarness.h"

using std::unordered_map;
using std::wstring;

namespace
{
test::Harness harness("VSTPluginCommandTests");

// paramMap lookup folded into the harness so a missing key fails loudly instead
// of default-inserting a 0.0f and silently passing.
float paramValue(const unordered_map<wstring, float>& paramMap, const wstring& name, const std::string& label)
{
	auto it = paramMap.find(name);
	harness.expectTrue(it != paramMap.end(), label + ": parameter not present");
	if (it == paramMap.end())
		return 0.0f;
	return it->second;
}

void testLibraryOnly()
{
	// Bare library reference: no chunk, no params. getInstance keeps the absolute
	// path as-is, and the serialized body is empty (store() emits only the Library
	// token in this case).
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll");
	harness.expectTrue(!cmd.libraryPath.empty(), "library-only: library resolved");
	harness.expectTrue(cmd.libraryPath == L"C:\\plugins\\reverb.dll", "library-only: lib path");
	harness.expectTrue(cmd.chunkData == L"", "library-only: no chunk data");
	harness.expectEqual(cmd.paramMap.size(), (size_t)0, "library-only: no params");
	harness.expectTrue(cmd.serialize() == L"", "library-only: empty body");
}

void testQuotedPath()
{
	// A path containing spaces is given quoted; splitQuoted strips the quotes and
	// keeps the spaces, and the parser keeps the absolute path verbatim.
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library \"C:\\Program Files\\My Plugin\\plugin.dll\"");
	harness.expectTrue(!cmd.libraryPath.empty(), "quoted-path: library resolved");
	harness.expectTrue(cmd.libraryPath == L"C:\\Program Files\\My Plugin\\plugin.dll", "quoted-path: lib path with spaces");
	harness.expectTrue(cmd.chunkData == L"", "quoted-path: no chunk data");
}

void testChunkData()
{
	// Library + ChunkData are mutually exclusive with the param list. The chunk is
	// stored verbatim and round-trips through ChunkData "...".
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll ChunkData \"QUJDRA==\"");
	harness.expectTrue(!cmd.libraryPath.empty(), "chunk: library resolved");
	harness.expectTrue(cmd.chunkData == L"QUJDRA==", "chunk: chunk data");
	harness.expectEqual(cmd.paramMap.size(), (size_t)0, "chunk: no params");
	harness.expectTrue(cmd.serialize() == L" ChunkData \"QUJDRA==\"", "chunk: serialized body");
}

void testNamedParams()
{
	// Named parameters: each pair is "<name> <numeric value>". The value's first
	// character is a digit, so the parser takes the isdigit branch that maps the
	// name (the key) to the value.
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll Gain 0.5 Mix 0.25");
	harness.expectTrue(!cmd.libraryPath.empty(), "named-params: library resolved");
	harness.expectTrue(cmd.chunkData == L"", "named-params: no chunk data");
	harness.expectEqual(cmd.paramMap.size(), (size_t)2, "named-params: two params");
	harness.expectEqual(paramValue(cmd.paramMap, L"Gain", "named-params Gain"), 0.5f, "named-params: Gain value");
	harness.expectEqual(paramValue(cmd.paramMap, L"Mix", "named-params Mix"), 0.25f, "named-params: Mix value");
}

void testQuotedName()
{
	// A parameter name containing a space is quoted; splitQuoted reassembles it as
	// a single key, and the numeric value still takes the isdigit branch.
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll \"Dry Wet\" 0.75");
	harness.expectEqual(cmd.paramMap.size(), (size_t)1, "quoted-name: one param");
	harness.expectEqual(paramValue(cmd.paramMap, L"Dry Wet", "quoted-name value"), 0.75f, "quoted-name: value");
}

void testIdParams()
{
	// Parameters addressed by numeric id behave the same as named ones: the id is
	// the key, the following numeric token is the value.
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll 0 0.1 12 0.9");
	harness.expectEqual(cmd.paramMap.size(), (size_t)2, "id-params: two params");
	harness.expectEqual(paramValue(cmd.paramMap, L"0", "id-params 0"), 0.1f, "id-params: id 0 value");
	harness.expectEqual(paramValue(cmd.paramMap, L"12", "id-params 12"), 0.9f, "id-params: id 12 value");
}

void testNonNumericValueBranch()
{
	// Covers the other side of the isdigit branch verbatim: when a value token is
	// not numeric, the parser treats that token as the parameter name and reads the
	// token two slots further on as its value. This is an edge of the original
	// grammar, asserted here only to lock the verbatim behaviour (store() never
	// emits such a line, so it is not part of the round-trip set).
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll ParamName SomeText 0.5");
	harness.expectEqual(cmd.paramMap.size(), (size_t)1, "non-numeric: one param");
	harness.expectEqual(paramValue(cmd.paramMap, L"SomeText", "non-numeric value"), 0.5f, "non-numeric: mapped value");
}

// Asserts that serializing a parsed command reproduces the expected canonical
// body, and that a second parse/serialize cycle is stable (the canonical form is
// a fixed point of the parser/serializer pair). The leading library path is held
// constant so only the chunk/param body varies.
void expectBodyRoundTrip(const wstring& parameters, const wstring& expectedBody)
{
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", parameters);
	wstring body = cmd.serialize();
	harness.expectTrue(body == expectedBody,
		"serialize(parse(...)) body mismatch for: " + std::string(parameters.begin(), parameters.end()));

	VSTPluginCommand reparsed = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll" + body);
	harness.expectTrue(reparsed.serialize() == expectedBody,
		"second round trip not stable for: " + std::string(parameters.begin(), parameters.end()));
}

void testSerializeRoundTrip()
{
	// Library-only -> empty body. ChunkData and single-param forms round-trip to
	// themselves. Multi-param maps are checked for stability separately because
	// unordered_map iteration order is unspecified.
	expectBodyRoundTrip(L"Library C:\\plugins\\reverb.dll", L"");
	expectBodyRoundTrip(L"Library C:\\plugins\\reverb.dll ChunkData \"QUJDRA==\"", L" ChunkData \"QUJDRA==\"");
	expectBodyRoundTrip(L"Library C:\\plugins\\reverb.dll Gain 0.5", L" Gain 0.5");

	// A quoted name with a space re-emits quoted, and a doubled "" inside the
	// quotes parses back to a single ", so the name survives a round trip.
	expectBodyRoundTrip(L"Library C:\\plugins\\reverb.dll \"Dry Wet\" 0.75", L" \"Dry Wet\" 0.75");

	// A multi-param command is not asserted against a fixed string (map order is
	// unspecified), but parse -> serialize -> parse must preserve the map.
	VSTPluginCommand cmd = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll Gain 0.5 Mix 0.25");
	wstring body = cmd.serialize();
	VSTPluginCommand reparsed = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll" + body);
	harness.expectEqual(reparsed.paramMap.size(), (size_t)2, "multi-param round trip: size");
	harness.expectEqual(paramValue(reparsed.paramMap, L"Gain", "multi-param Gain"), 0.5f, "multi-param round trip: Gain");
	harness.expectEqual(paramValue(reparsed.paramMap, L"Mix", "multi-param Mix"), 0.25f, "multi-param round trip: Mix");

	// A hand-built command serializes and parses back to the same single param,
	// exercising the struct -> string -> struct direction as well.
	VSTPluginCommand built;
	built.libraryPath = L"C:\\plugins\\reverb.dll";
	built.paramMap[L"Feedback"] = 0.6f;
	wstring builtBody = built.serialize();
	harness.expectTrue(builtBody == L" Feedback 0.6", "hand-built body");
	VSTPluginCommand builtReparsed = VSTPluginCommand::parse(L"", L"Library C:\\plugins\\reverb.dll" + builtBody);
	harness.expectEqual(paramValue(builtReparsed.paramMap, L"Feedback", "hand-built Feedback"), 0.6f, "hand-built round trip");
}
}

void runVSTPluginCommandTests()
{
	testLibraryOnly();
	testQuotedPath();
	testChunkData();
	testNamedParams();
	testQuotedName();
	testIdParams();
	testNonNumericValueBranch();
	testSerializeRoundTrip();
	harness.report();
}
