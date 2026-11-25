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

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "filters/ConfigFileReference.h"
#include "IFilterFactory.h"
#include "ChannelRoutingPlan.h"
#include "FilterConfiguration.h"
#include "ConfigSwapChannel.h"
#include "parser/EngineParser.h"
#include "diagnostics/performance/BlockTiming.h"
#include "diagnostics/performance/PrecisionTimer.h"

struct ConfigLoadTraceEntry;
class ConfigLoadTraceSink;
class IInputTap;
class IRegistry;

enum class EngineHost
{
	Apo,
	AsioHost,
	Editor,
	Test
};

// Everything initialize() needs, in one value (audit #250 A6). The old shape
// was three calls whose ordering lived only in comments (setPreMix and
// setDeviceInfo before initialize) and whose adjacent same-typed parameters
// were silently swappable; twelve call sites each re-assembled the ritual,
// with six different spellings of the deviceString the Device: command
// matches against. The engine now assembles deviceString from the parts
// (connection name, device name, then the GUID when present - the
// production spelling), and the config source is explicit instead of a
// sentinel: an empty customPath means "the registry's ConfigPath, with the
// change watcher" (the APO's mode); a caller-supplied path loads that file
// and starts no watcher.
struct EngineSetup
{
	float sampleRate = 48000.0f;
	unsigned inputChannelCount = 0;
	unsigned realChannelCount = 0;
	unsigned outputChannelCount = 0;
	unsigned channelMask = 0;
	unsigned maxFrameCount = 0;
	std::wstring customPath;
	bool preMix = false;
	bool capture = false;
	bool postMixInstalled = true;
	bool resampleReceived = false;
	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	EngineHost host = EngineHost::Test;
	// Prefix of the Send ring's mapping and event names. The audio service
	// uses the default; tests use their own so they never meet a live APO.
	std::wstring sendNamePrefix = L"Local\\EAPO.Send.";
	// Registry port for the ConfigPath read and the config language's
	// readRegString/readRegDWORD; null = the live registry.
	IRegistry* registry = nullptr;
};

#pragma AVRT_VTABLES_BEGIN
class FilterEngine
{
public:
	FilterEngine();
	~FilterEngine();

	void initialize(const EngineSetup& setup);
	// The injected registry (audit #250 A6/A3): the live adapter unless the
	// setup supplied a fake. Consumed by the ConfigPath read and the config
	// language's readRegString/readRegDWORD.
	IRegistry& registryAccess() const;
	// Waits for the previous publication to be consumed, then builds and
	// publishes one complete configuration. A failed load keeps the active
	// configuration and returns false; no initialization exception is allowed to
	// escape the configuration-loading boundary.
	bool loadConfig(const std::wstring& customPath = L"");
	// Takes the judged file: its handles are let go once the bytes are read,
	// before the lines in it are parsed (issue #477).
	void loadConfigFile(JudgedPath path);
	ConfigFileReference::Target judgeIncludedFile(const std::wstring& configPath, const std::wstring& written);
	void watchRegistryKey(const std::wstring& key);
	void watchEvent(const std::wstring& name);
	// The factory that installs the tap lives as long as the engine, so the tap
	// must outlive processing. The tap runs after the configuration read and its
	// virtual-channel zero fill (rather than literally immediately after read),
	// which keeps targets at indices >= realChannelCount from being wiped.
	void setInputTap(IInputTap* tap) {inputTap.store(tap, std::memory_order_release);}
	// Read only from the audio thread (filters' process()).
	uint64_t blockCounter() const {return processBlockCounter;}
	// Four surfaces: float interleaved (the APO's usual connection format),
	// float planar (VoicemeeterClient, whose host hands per-channel pointer
	// arrays), double interleaved (the APO's double connection, Benchmark, the
	// Editor's analysis engine), and double planar (the ASIO stream processors
	// asio/EngineHostCore.cpp and asio/InProcProcessor.cpp). Both planar surfaces
	// read realChannelCount input pointers and write outputChannelCount output
	// pointers.
	void process(float* output, float* input, unsigned frameCount);
	void process(float** output, float** input, unsigned frameCount);
	void process(double* output, double* input, unsigned frameCount);
	void process(double** output, double** input, unsigned frameCount);
	// Only a host's real-time callback calls this (the APO), once per callback.
	void noteHostBlock(int64_t entryTicks, int64_t exitTicks, unsigned frameCount) noexcept;
	// Reports and clears host callback timing from a control thread.
	void reportHostBlockTiming(const wchar_t* when);

	bool isPreMix() const {return preMix;}
	bool isCapture() const {return capture;}
	bool isPostMixInstalled() const {return postMixInstalled;}
	bool resamplesReceivedAudio() const {return resampleReceived;}
	EngineHost getHost() const {return host;}
	const std::wstring& getSendNamePrefix() const {return sendNamePrefix;}
	const std::wstring& getDeviceName() const {return deviceName;}
	const std::wstring& getConnectionName() const {return connectionName;}
	const std::wstring& getDeviceGuid() const {return deviceGuid;}
	const std::wstring& getDeviceString() const {return deviceString;}
	unsigned getInputChannelCount() const {return inputChannelCount;}
	unsigned getRealChannelCount() const {return realChannelCount;}
	unsigned getOutputChannelCount() const {return outputChannelCount;}
	unsigned getChannelMask() const {return channelMask;}
	float getSampleRate() const {return sampleRate;}
	unsigned getMaxFrameCount() const {return maxFrameCount;}
	// Fixed delay, in frames, added by the configuration that most recently
	// became the processing configuration. 0 before the first one. Safe to call
	// from any thread.
	unsigned getRealLatency() const noexcept { return realLatency.load(std::memory_order_relaxed); }
	// Per output channel. Not synchronised with the audio thread: call it only
	// while processing is not running concurrently (Benchmark, tests).
	std::vector<unsigned> getRealLatencyByChannel() const;
	std::vector<std::wstring> deviceChannelNames() const;
	// Meaningful only while a configuration load runs on the control thread.
	const std::vector<std::wstring>& loadingChannelNames() const;
	// The three stream facts a FilterConfiguration is built for (audit #250 A2).
	EngineStreamFormat streamFormat() const {return {realChannelCount, outputChannelCount, maxFrameCount};}
	// Crossfade length in samples for a configuration swap, set by initialize().
	// Exposed so tests exercise the real value instead of re-deriving the
	// sampleRate / 100 formula.
	unsigned getTransitionLength() const {return transitionLength;}
	EngineParser* getParser() {return &parser;}
	// Attach before initialize()/loadConfig(); entries describe every load
	// that runs while attached. The engine does not own the sink; pass nullptr
	// to detach. The Editor's analysis engine is the only expected consumer -
	// the APO runtime never attaches one.
	void setLoadTraceSink(ConfigLoadTraceSink* sink) {traceSink = sink;}
	// Analysis owns a deterministic view of time-varying filters. Set before
	// initialize(), so factories can freeze their dynamic state while loading.
	void setAnalysisMode(bool enabled) {analysisMode = enabled;}
	bool isAnalysisMode() const {return analysisMode;}
	void markFrozenDynamicAnalysis() {load.frozenDynamicAnalysis = true;}
	bool usedFrozenDynamicAnalysis() const {return load.frozenDynamicAnalysis;}
	// Factories report an evaluation fact for the line currently being
	// parsed; the engine stamps the file/line position. No-op without a sink.
	void traceLoadEvent(ConfigLoadTraceEntry entry);
	// A factory saying "this line was mine and its parameters are wrong". Stamps
	// the current file and line, logs it, and passes it to the trace sink so the
	// Editor can mark the row. The dispatch loop calls this for a Malformed
	// LineOutcome. A nonzero line stamps that line of the current file instead: an If that
	// the end of its file shows to be unclosed is reported on its own line.
	void reportParseError(const std::wstring& command, const std::wstring& reason, int line = 0);
	// Device, If and Stage keep no load state in their factory objects. These
	// operations mutate the current LoadSession, so an exception discards every
	// unfinished gate together with the rest of that load (audit #467 TD-45).
	void setGate(ConfigGateKind kind, bool open);
	void pushIfGate(bool open, bool branchTaken);
	void setIfBranchTaken(bool branchTaken);
	void popIfGate();
	bool hasIfGate() const;
	bool ifGatesOpen() const;
	bool ifGateParentOpen() const;
	bool ifBranchTaken() const;
	// Side effects that must happen only after the complete configuration has
	// been published. Send uses this for ring metadata and closure.
	void deferUntilConfigurationCommit(std::function<void()> action);
	// The 1-based line of the current file the load is at, for a factory that
	// has to report on a line later (the If family).
	int loadTraceLine() const
	{
		return load.traceLine;
	}
	// Returns true if the active configuration (or any transition target) carries
	// state across blocks or has a tail. Used by the APO to skip processing on
	// silent input when safe. Conservative: returns true while a config swap is
	// pending or before initialize() has completed.
	bool hasStatefulOrTailFilters() const;

private:
	void loadConfigFile(const std::wstring& path);
	void loadConfigStream(const std::wstring& path, std::stringstream inputStream);
	struct FilterConfigurationDeleter
	{
		void operator()(FilterConfiguration* config) const;
	};
	using FilterConfigurationPtr = std::unique_ptr<FilterConfiguration, FilterConfigurationDeleter>;

	void addFilters(FilterVector filters);
	static void notificationThread(FilterEngine* engine);
	void finishTransitionIfReady();
	// The single choreography behind the four public process() overloads;
	// IoTraits carries the per-layout bypass copy and configuration read/write.
	// Defined and instantiated only in engine/FilterEngine.Process.cpp.
	template <typename IoTraits, typename SampleType>
	void processImpl(SampleType output, SampleType input, unsigned frameCount);

	std::vector<std::unique_ptr<IFilterFactory>> factories;

	bool preMix;
	bool capture;
	bool postMixInstalled;
	bool resampleReceived = false;
	EngineHost host = EngineHost::Test;
	std::wstring sendNamePrefix = L"Local\\EAPO.Send.";
	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	std::wstring deviceString;
	std::wstring configPath;
	// Set by initialize() from the setup; null until then (registryAccess()
	// falls back to the live registry).
	IRegistry* registryPort = nullptr;
	float sampleRate = 0.0f;
	// number of input channels that originally existed before child APO processing
	unsigned inputChannelCount;
	// number of input channels in process (including channels coming from child APO output)
	unsigned realChannelCount;
	unsigned outputChannelCount;
	unsigned channelMask = 0;
	unsigned maxFrameCount = 0;

	// Pre-init inputs that survive across loads: the trace sink and analysis
	// mode are attached before initialize() and describe every load that
	// runs while attached.
	ConfigLoadTraceSink* traceSink = nullptr;
	bool analysisMode = false;

	struct ConfigGateStack
	{
		struct IfGate
		{
			bool open = false;
			bool branchTaken = false;
			int openingLine = 0;
		};

		bool deviceOpen = true;
		bool stageOpen = true;
		std::vector<IfGate> ifGates;
		// An included file may see outer active If gates but cannot close them.
		size_t fileIfBase = 0;

		bool allows(ConfigGateBoundary boundary) const;
		bool allIfOpen() const;
		bool parentIfOpen() const;
		void beginFile();
		std::vector<int> unclosedIfLines() const;
	};

	// Everything a configuration load builds through, as one value (audit
	// #250 A1 and #467 TD-45). loadConfig's transaction is a move of the whole
	// session, so unfinished filters, gates and deferred commit actions are all
	// discarded together when a load throws.
	// The registry keys, named events and frozen-analysis flag are load results
	// read afterward: the watcher thread snapshots both watch sets under
	// loadMutex, and the Editor's analysis reads the freeze flag.
	struct LoadSession
	{
		std::vector<std::unique_ptr<FilterInfo>> filterInfos;
		// Which channel names each filter sees and where its buffers sit
		// (audit #348 F1); its lastInPlace carries across loads.
		ChannelRoutingPlan routing;
		ConfigGateStack gates;
		std::vector<std::function<void()>> commitActions;
		std::unordered_set<std::wstring> watchRegistryKeys;
		std::unordered_set<std::wstring> watchEvents;
		// Position of the line loadConfigFile is currently feeding to the
		// factories; saved/restored across Include recursion together with the
		// channel names and gates. Only meaningful while a sink is attached.
		std::wstring traceFile;
		int traceLine = 0;
		bool frozenDynamicAnalysis = false;
	};
	LoadSession load;
	EngineParser parser;

	ConfigSwapChannel<FilterConfigurationPtr> configChannel;
	std::atomic<unsigned> realLatency{0};
	std::atomic<IInputTap*> inputTap{nullptr};
	eapo::diag::BlockTimingMonitor hostBlockTiming;
	int64_t performanceFrequency = 0;
	std::atomic<uint64_t> oversizedBlockCount{0};
	std::atomic<unsigned> lastOversizedFrameCount{0};
	std::atomic<unsigned> lastOversizedMaxFrameCount{0};
	uint64_t reportedOversizedBlockCount = 0;
	// Realtime-thread-only token. Incremented once at processImpl entry.
	uint64_t processBlockCounter = 0;

	unsigned transitionCounter;
	unsigned transitionLength = 0;
	// Precomputed equal-power crossfade factors of length transitionLength.
	// Recomputed by initialize() whenever transitionLength changes.
	std::vector<double> transitionFactorTable;
	std::mutex loadMutex;
	PrecisionTimer timer;
	std::thread notificationWorker;
};
#pragma AVRT_VTABLES_END
