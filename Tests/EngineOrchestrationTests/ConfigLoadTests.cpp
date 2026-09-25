/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What loading a configuration reports and resolves: the per-line load
	trace (If/Eval/inline values), parse errors on the line they belong to
	and not on prose, a filter that fails to set up, control-flow and Stage
	mistakes, the Include nesting limit, refused network paths, Include's
	quotes and variables, registry reads through the injected port, and
	analysis mode's frozen Dynamic Velvet.
*/

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ConfigLoadTrace.h"
#include "engine/IFilterFactory.h"
#include "filters/FilterFactoryRegistry.h"
#include "Tests/FakeRegistry.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
// A filter whose setup throws, for the one test below. Registered like any
// factory, so every engine in this process has it; no other config names it.
class ThrowingSetupFilter : public IFilter
{
public:
	std::vector<std::wstring> initialize(float, unsigned, std::vector<std::wstring>) override
	{
		throw std::runtime_error("the test filter refuses to set up");
	}

	void process(double**, double**, unsigned) override
	{
	}
};

class ThrowingSetupFactory : public IFilterFactory
{
public:
	FilterVector createFilter(const std::wstring&, std::wstring& command, std::wstring&) override
	{
		if (command != L"EapoTestThrowOnSetup")
			return {};
		return singleFilter(makeFilter<ThrowingSetupFilter>());
	}
};

REGISTER_FILTER_FACTORY(1000, ThrowingSetupFactory, L"EapoTestThrowOnSetup")

struct TraceCollector : ConfigLoadTraceSink
{
	std::vector<ConfigLoadTraceEntry> entries;
	void addEntry(const ConfigLoadTraceEntry& entry) override
	{
		entries.push_back(entry);
	}
};
}

// The engine reports per-line facts while loading
// (branch decisions, Eval values, swallowed lines) through an attached
// ConfigLoadTraceSink so the Editor can echo them next to the config rows.
// This pins the whole grammar over one representative config: an Eval, a
// taken If with a nested false If inside, a short-circuited ElseIf, a dead
// Else, inline `expression` substitution, and file/line stamping.
void testConfigLoadTrace(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"trace.txt",
		"Eval: x = 2 + 3\n"          // line 1: Eval -> "5"
		"If: x == 5\n"               // line 2: Condition true
		"Preamp: -3 dB\n"            // line 3: executes, no entry
		"If: x > 100\n"              // line 4: Condition false
		"Delay: 1 ms\n"              // line 5: SkippedLine
		"EndIf:\n"                   // line 6: closes nested scope, no entry
		"ElseIf: x == 4\n"           // line 7: chain satisfied -> NotEvaluated
		"Preamp: -1 dB\n"            // line 8: SkippedLine
		"Else:\n"                    // line 9: ElseBranch, inactive
		"Preamp: -2 dB\n"            // line 10: SkippedLine
		"EndIf:\n"                   // line 11: closes outer scope, no entry
		"Preamp: `x - 10` dB\n");    // line 12: InlineValue "-5 dB"

	TraceCollector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 512, configPath);

	const std::vector<ConfigLoadTraceEntry>& entries = collector.entries;
	harness.requireEqual((int)entries.size(), 9, "load trace entry count");

	auto expectEntry = [&](int index, int line, ConfigLoadTraceEntry::Kind kind,
		ConfigLoadTraceEntry::Result result, bool active, const std::string& what) {
		const ConfigLoadTraceEntry& entry = entries[(size_t)index];
		harness.expectEqual(entry.line, line, what + ": line");
		harness.expect(entry.kind == kind, what + ": kind");
		harness.expect(entry.result == result, what + ": result");
		harness.expectEqual(entry.active, active, what + ": active");
		harness.expect(entry.file == configPath, what + ": file stamp");
	};

	expectEntry(0, 1, ConfigLoadTraceEntry::Kind::Eval, ConfigLoadTraceEntry::Result::NotEvaluated, false, "Eval");
	harness.expect(collector.entries[0].text == L"5", "Eval reports the computed value");
	harness.expectFalse(collector.entries[0].error, "Eval reports no error");
	expectEntry(1, 2, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::True, true, "outer If");
	expectEntry(2, 4, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::False, false, "nested If");
	expectEntry(3, 5, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Delay");
	expectEntry(4, 7, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false, "short-circuited ElseIf");
	expectEntry(5, 8, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Preamp");
	expectEntry(6, 9, ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false, "dead Else");
	expectEntry(7, 10, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Preamp inside the dead Else");
	expectEntry(8, 12, ConfigLoadTraceEntry::Kind::InlineValue, ConfigLoadTraceEntry::Result::NotEvaluated, false, "inline value");
	// The engine trims only the command key, not the parameter text, so the
	// substituted string keeps the space after the colon - the entry reports
	// exactly what the downstream factories saw.
	harness.expect(collector.entries[8].text == L" -5 dB", "inline substitution reports the resolved parameters");

	// A second load without a sink must not crash and must add nothing.
	engine.setLoadTraceSink(nullptr);
	engine.loadConfig(configPath);
	harness.expectEqual((int)collector.entries.size(), (int)entries.size(),
		"detached sink receives nothing on a reload");
}

// The parse-error channel that replaced the engine's guess. What matters is the
// pair of judgements: a factory's own broken line is reported, and a line no
// factory claimed is not - because prose and notes are how 1.4.2 configurations
// carry comments, and reporting those would bury the real diagnostics.
void testParseErrorsAreReportedPerLineAndProseIsNot(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"parse-errors.txt",
		"Convolution:\n"                    // line 1: its own command, no path
		"Channel:\n"                        // line 2: its own command, no channel
		"Copy:\n"                           // line 3: its own command, no assignment
		"GraphicEQ:\n"                      // line 4: its own command, no nodes
		// The pre-S7 factories joined the reporting generation in audit #275
		// (TD-03); the four lines below used to fail in silence or log-only.
		"Filter 1: ON PK Fc 1000 Hz\n"      // line 5: peaking without gain or Q
		"Filter 2: ON IIR Order 2\n"        // line 6: IIR without coefficients (one report, not a BiQuad echo)
		"Delay: quickly\n"                  // line 7: delay without a parsable amount
		"Preamp: loud\n"                    // line 8: preamp without a parsable gain
		"remember to try 2 dB less here\n"  // line 9: prose, not an error
		"copy: a note to self\n"            // line 10: wrong case, so prose
		"Filter 3: OFF PK Fc 99999999 Hz\n" // line 11: disabled, so a no-op rather than an error
		"Preamp: -3 dB\n");                 // line 12: fine, and must still run

	TraceCollector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 512, configPath);

	std::vector<int> errorLines;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind != ConfigLoadTraceEntry::Kind::ParseError)
			continue;
		harness.expect(entry.error, "a parse error is flagged as one");
		harness.expect(!entry.text.empty(), "and carries a reason, which is the whole point of moving the diagnosis into the factory");
		harness.expect(entry.file == configPath, "stamped with the file it is in");
		errorLines.push_back(entry.line);
	}

	harness.requireEqual(errorLines.size(), size_t(8),
		"one report per broken line, none for the prose lines, none for the disabled filter, "
		"and no BiQuad echo of the broken IIR line");
	for (int line = 0; line < 8; line++)
		harness.expectEqual(errorLines[(size_t)line], line + 1,
			"the reports land on the lines that are broken, in order");

	// The load kept going: half a configuration is still worth running, and a
	// broken line must not take the working ones below it with it. loadConfig
	// answers false only when the whole load failed.
	harness.expect(engine.loadConfig(configPath),
		"a configuration with eight unusable lines still loads, because the working lines below them have to run");
}

// Audit #348 TD-18: a filter that throws while being set up rolls the whole
// load back, as before, and now names the line it came from.
void testFilterSetupFailureNamesItsLine(test::Harness& harness)
{
	const std::wstring good = writeConfig(harness, L"setup-good.txt", "Preamp: -6.0206 dB\n");
	const std::wstring bad = writeConfig(harness, L"setup-bad.txt",
		"Preamp: -20 dB\n"               // line 1: fine
		"EapoTestThrowOnSetup: now\n");  // line 2: its filter throws in initialize

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, good);
	TraceCollector collector;
	engine.setLoadTraceSink(&collector);

	harness.expectFalse(engine.loadConfig(bad), "a load whose filter cannot be set up fails as a whole");
	const ConfigLoadTraceEntry* setup = nullptr;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::SetupError)
			setup = &entry;
	}
	harness.require(setup != nullptr, "the failure is on the load trace");
	harness.expectEqual(setup->line, 2, "on the line whose filter threw");
	harness.expect(setup->file == bad, "in the file it is in");
	harness.expect(setup->error, "flagged as an error");
	harness.expect(setup->text.find(L"the test filter refuses to set up") != std::wstring::npos,
		"carrying the reason the filter gave");

	const std::vector<float> after = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(after[0] - 0.5f) < 1e-3f, "and the previous configuration keeps running");
}

// Audit #348 TD-18: these reached only the log; now they are on the line.
void testControlFlowAndStageMistakesAreReported(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"control-mistakes.txt",
		"Stage: pre-mix premix\n"  // line 1: one unknown stage among known ones
		"ElseIf: 1\n"              // line 2: no If before it
		"Else:\n"                  // line 3: no If before it
		"EndIf:\n"                 // line 4: no If before it
		"If: 1\n"                  // line 5: never closed
		"Preamp: -3 dB\n");        // line 6

	FilterEngine engine;
	TraceCollector collector;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 480, configPath);

	std::vector<int> lines;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError)
			lines.push_back(entry.line);
	}
	harness.requireEqual(lines.size(), size_t(5), "an unknown stage, three strays and the unclosed If");
	harness.expectEqual(lines[0], 1, "the unknown stage on its line");
	harness.expectEqual(lines[1], 2, "the stray ElseIf on its line");
	harness.expectEqual(lines[2], 3, "the stray Else on its line");
	harness.expectEqual(lines[3], 4, "the stray EndIf on its line");
	harness.expectEqual(lines[4], 5, "the unclosed If on its own line, where the Editor has a row for it");
}

void testIncludeRecursionLimitIsReported(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"self-include.txt", "Include: self-include.txt\n");

	FilterEngine engine;
	TraceCollector collector;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 480, configPath);

	size_t reports = 0;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError && entry.text.find(L"nested") != std::wstring::npos)
			reports++;
	}
	harness.expectEqual(reports, size_t(1), "the include that hits the nesting limit is reported once");
}

void testConfigReferencedRemotePathsAreRefused(test::Harness& harness)
{
	writeConfig(harness, L"local-included.txt", "Preamp: -1 dB\n");
	const std::wstring configPath = writeConfig(harness, L"remote-paths.txt",
		"Include: \\\\eapo-policy-test.invalid\\share\\nested.txt\n"
		"Convolution: \\\\eapo-policy-test.invalid\\share\\ir.wav\n"
		"MultiConvolution: L=0 \\\\eapo-policy-test.invalid\\share\\ir.wav\n"
		"SubwooferRouting: Profile \\\\eapo-policy-test.invalid\\share\\p.swxt.json\n"
		"VSTPlugin: Library \\\\eapo-policy-test.invalid\\share\\plugin.dll\n"
		"Include: local-included.txt\n"
		"Preamp: -3 dB\n");

	TraceCollector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 512, configPath);

	std::vector<const ConfigLoadTraceEntry*> errors;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError)
			errors.push_back(&entry);
	}

	harness.requireEqual(errors.size(), size_t(5),
		"the five remote references are refused and the local lines still load");
	for (size_t index = 0; index < errors.size(); ++index)
	{
		const ConfigLoadTraceEntry& entry = *errors[index];
		harness.expectEqual(entry.line, static_cast<int>(index + 1),
			"remote path errors are reported on lines 1 through 5 in order");
		harness.expect(entry.error, "a remote path refusal is flagged as an error");
		harness.expect(entry.file == configPath, "a remote path refusal names its configuration file");
		harness.expect(entry.text.find(L"network share") != std::wstring::npos,
			"a remote path refusal identifies the network-share policy");
	}

	harness.expect(engine.loadConfig(configPath),
		"a configuration with refused remote references and working local lines still loads");
}

// Audit #348 A1: Include reads its file argument by the rule every file a
// line names shares, so the quotes and %VARIABLES% Convolution always took
// work here too, and an Include with nothing after it is reported rather
// than loading the configuration's own folder.
void testIncludeTakesQuotesAndVariables(test::Harness& harness)
{
	writeConfig(harness, L"included with spaces.txt", "Preamp: -6.0206 dB\n");
	writeConfig(harness, L"included-by-variable.txt", "Preamp: -6.0206 dB\n");
	_wputenv_s(L"EAPO_XT_TEST_INCLUDE_DIR", testDirectory().c_str());
	const std::wstring configPath = writeConfig(harness, L"include-dialect.txt",
		"Include: \"included with spaces.txt\"\n"
		"Include: %EAPO_XT_TEST_INCLUDE_DIR%\\included-by-variable.txt\n"
		"Include:   \n");

	TraceCollector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 480, configPath);
	const std::vector<float> output = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(output[(size_t)478 * 2] - 0.25f) < 1e-3f,
		"the quoted include and the include through a variable both applied their -6 dB");

	std::vector<const ConfigLoadTraceEntry*> errors;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError)
			errors.push_back(&entry);
	}
	harness.requireEqual(errors.size(), size_t(1), "only the empty include is an error");
	harness.expectEqual(errors[0]->line, 3, "reported on its own line");
	harness.expect(errors[0]->text.find(L"expected the path of a configuration file") != std::wstring::npos,
		"as a missing path");
	_wputenv_s(L"EAPO_XT_TEST_INCLUDE_DIR", L"");
}

// Audit #250 A6/A3: the engine's registry surface (the config language's
// readRegDWORD here) goes through the injected port, so a config that reads
// the registry is deterministic under a fake - previously these functions
// could only ever see the live machine.
void testConfigRegistryReadsGoThroughThePort(test::Harness& harness)
{
	test::FakeRegistry registry;
	const std::wstring key = L"HKEY_CURRENT_USER\\Software\\EapoPortTest";
	registry.seedKey(key);
	registry.seedDword(key, L"gain", 6);

	const std::wstring configPath = writeConfig(harness, L"registry-port.txt",
		"Eval: g=readRegDWORD(\"HKEY_CURRENT_USER\\\\Software\\\\EapoPortTest\", \"gain\")\n"
		"Preamp: `-g` dB\n");
	FilterEngine engine;
	// The load resolves g = 6 from the seeded value and the preamp
	// attenuates by 6 dB.
	EngineSetup setup;
	setup.sampleRate = 48000.0f;
	setup.inputChannelCount = 2;
	setup.realChannelCount = 2;
	setup.outputChannelCount = 2;
	setup.maxFrameCount = 16;
	setup.customPath = configPath;
	setup.deviceName = L"PortTest";
	setup.connectionName = L"File";
	setup.registry = &registry;
	engine.initialize(setup);

	const std::vector<float> output = processDcBlock(engine, 1.0f, 1.0f, 16);
	const float expected = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
	harness.expect(std::fabs(output[(size_t)15 * 2] - expected) < 1e-4f,
		"readRegDWORD resolves through the injected fake registry");
}

void testAnalysisFreezesDynamicVelvetAndLabelsTheSnapshot(test::Harness& harness)
{
	const std::wstring dynamicPath = writeConfig(harness, L"analysis-velvet-dynamic.txt",
		"Velvet: Mode=Dynamic Amount=100% Length=27.5625ms "
		"Density=1088.435/s Evolution=5s Transition=250ms "
		"Decay=-60dB Variation=2050083136\n");
	const std::wstring staticPath = writeConfig(harness, L"analysis-velvet-static.txt",
		"Velvet: Mode=Static Amount=100% Length=27.5625ms "
		"Density=1088.435/s Evolution=5s Transition=250ms "
		"Decay=-60dB Variation=2050083136\n");

	FilterEngine analysis;
	analysis.setAnalysisMode(true);
	initializeEngine(analysis, 48000, 2, 256, dynamicPath);
	harness.expect(analysis.usedFrozenDynamicAnalysis(),
		"analysis freezes Dynamic Velvet to one deterministic kernel and labels the response");
	harness.expect(analysis.loadConfig(staticPath),
		"analysis can reload a static Velvet configuration");
	harness.expect(!analysis.usedFrozenDynamicAnalysis(),
		"a static Velvet response is not labelled as a frozen dynamic snapshot");

	FilterEngine runtime;
	initializeEngine(runtime, 48000, 2, 256, dynamicPath);
	harness.expect(!runtime.usedFrozenDynamicAnalysis(),
		"the real-time engine keeps Dynamic Velvet live and never reports an analysis snapshot");
}
