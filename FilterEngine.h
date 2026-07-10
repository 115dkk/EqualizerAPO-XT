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

#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "IFilterFactory.h"
#include "FilterConfiguration.h"
#include "helpers/PrecisionTimer.h"
#include "helpers/MemoryHelper.h"
#include "helpers/Win32Event.h"

namespace mup {
class ParserX;
}

struct ConfigLoadTraceEntry;
class ConfigLoadTraceSink;

#pragma AVRT_VTABLES_BEGIN
class FilterEngine
{
public:
	FilterEngine();
	~FilterEngine();

	void setPreMix(bool preMix);
	void setDeviceInfo(bool capture, bool postMixInstalled, const std::wstring& deviceName, const std::wstring& connectionName, const std::wstring& deviceGuid, const std::wstring& deviceString);
	void initialize(float sampleRate, unsigned inputChannelCount, unsigned realChannelCount, unsigned outputChannelCount, unsigned channelMask, unsigned maxFrameCount, const std::wstring& customPath = L"");
	void loadConfig(const std::wstring& customPath = L"");
	void loadConfigFile(const std::wstring& path);
	void watchRegistryKey(const std::wstring& key);
	void process(float* output, float* input, unsigned frameCount);
	void process(float** output, float** input, unsigned frameCount);
	void process(double* output, double* input, unsigned frameCount);
	void process(double** output, double** input, unsigned frameCount);

	bool isPreMix() const {return preMix;}
	bool isCapture() const {return capture;}
	bool isPostMixInstalled() const {return postMixInstalled;}
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
	// Crossfade length in samples for a configuration swap, set by initialize().
	// Exposed so tests exercise the real value instead of re-deriving the
	// sampleRate / 100 formula. (audit #146 TD033)
	unsigned getTransitionLength() const {return transitionLength;}
	mup::ParserX* getParser() {return parser.get();}
	// Attach before initialize()/loadConfig(); entries describe every load
	// that runs while attached. The engine does not own the sink; pass nullptr
	// to detach. The Editor's analysis engine is the only expected consumer -
	// the APO runtime never attaches one.
	void setLoadTraceSink(ConfigLoadTraceSink* sink) {traceSink = sink;}
	// Factories report an evaluation fact for the line currently being
	// parsed; the engine stamps the file/line position. No-op without a sink.
	void traceLoadEvent(ConfigLoadTraceEntry entry);
	// Returns true if the active configuration (or any transition target) carries
	// state across blocks or has a tail. Used by the APO to skip processing on
	// silent input when safe. Conservative: returns true while a config swap is
	// pending or before initialize() has completed.
	bool hasStatefulOrTailFilters() const;

private:
	struct FilterConfigurationDeleter
	{
		void operator()(FilterConfiguration* config) const;
	};
	using FilterConfigurationPtr = std::unique_ptr<FilterConfiguration, FilterConfigurationDeleter>;

	void addFilters(const std::vector<IFilter*>& filters);
	void cleanupConfigurations();
	static void notificationThread(FilterEngine* engine);
	bool acquireLoadPermit();
	void releaseLoadPermit();
	void finishTransitionIfReady();
	// The single choreography behind the four public process() overloads;
	// IoTraits carries the per-layout bypass copy and configuration read/write.
	// Defined and instantiated only in engine/FilterEngine.Process.cpp.
	// (audit #146 TD007)
	template <typename IoTraits, typename SampleType>
	void processImpl(SampleType output, SampleType input, unsigned frameCount);

	std::vector<std::unique_ptr<IFilterFactory>> factories;

	bool preMix;
	bool capture;
	bool postMixInstalled;
	std::wstring deviceName;
	std::wstring connectionName;
	std::wstring deviceGuid;
	std::wstring deviceString;
	std::wstring configPath;
	float sampleRate = 0.0f;
	// number of input channels that originally existed before child APO processing
	unsigned inputChannelCount;
	// number of input channels in process (including channels coming from child APO output)
	unsigned realChannelCount;
	unsigned outputChannelCount;
	unsigned channelMask = 0;
	unsigned maxFrameCount = 0;

	// only used during loading
	ConfigLoadTraceSink* traceSink = nullptr;
	// Position of the line loadConfigFile is currently feeding to the
	// factories; saved/restored across Include recursion like the channel
	// names. Only meaningful while a sink is attached.
	std::wstring traceFile;
	int traceLine = 0;
	std::vector<std::unique_ptr<FilterInfo>> filterInfos;
	std::vector<std::wstring> currentChannelNames;
	std::vector<std::wstring> lastChannelNames;
	std::vector<std::wstring> lastNewChannelNames;
	std::vector<std::wstring> allChannelNames;
	bool lastInPlace = false;
	std::unique_ptr<mup::ParserX> parser;

	FilterConfigurationPtr currentConfig;
	FilterConfigurationPtr nextConfig;
	FilterConfigurationPtr previousConfig;
	// Publication flag for the nextConfig worker->RT handoff. The notification
	// worker builds a FilterConfiguration and stores it into nextConfig under
	// loadMutex; the real-time process() thread reads nextConfig lock-free. As
	// nextConfig is a plain unique_ptr, on a weakly-ordered CPU (ARM64) the RT
	// thread could see the new pointer before the configuration's construction is
	// visible. This atomic carries the release(worker)/acquire(RT) edge: the RT
	// dereferences nextConfig only after acquire-loading true here. On x86/x64
	// (TSO) it lowers to plain loads/stores, so the existing platforms are
	// unaffected.
	std::atomic<bool> nextConfigReady{ false };

	unsigned transitionCounter;
	unsigned transitionLength = 0;
	// Precomputed equal-power crossfade factors of length transitionLength.
	// Recomputed by initialize() whenever transitionLength changes.
	std::vector<double> transitionFactorTable;
	std::mutex loadMutex;
	std::mutex loadPermitMutex;
	std::condition_variable loadPermitCv;
	bool loadPermitAvailable;
	PrecisionTimer timer;
	std::thread notificationWorker;
	std::unique_ptr<Win32Event> shutdownEvent;
	bool shutdownRequested;
	std::unordered_set<std::wstring> watchRegistryKeys;
	bool lastInputWasSilent;
};
#pragma AVRT_VTABLES_END
