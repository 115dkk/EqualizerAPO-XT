/*
    RealVstProbe - one-off engineering-evidence harness.

    Loads a real third-party VST3 plugin by path through the engine's public
    host classes (VSTPluginLibrary + VSTPluginFilter), simulates an N-channel
    Equalizer APO device playing a stereo music signal, and reports the level
    every output channel actually carries. This exercises the same code path
    the APO runs on a real machine, so a channel that stays silent here stays
    silent on that machine's speaker as well.

    A plugin fresh out of the factory may default to a stereo output mode (the
    OpenSpatial Upmixer's OUTPUT selector is normally set in its GUI). When the
    default state leaves the surround channels silent, the probe enumerates the
    plugin's parameters, finds discrete selectors whose value strings look like
    surround layouts (7.1 / 5.1) or whose titles look like an output switch,
    and retries with each candidate configuration - the GUI-less equivalent of
    the user picking OUTPUT 5.1/7.1 in the editor.

    Usage: RealVstProbe.exe <plugin path> <channel count>

    Exit code 0 (PASS): in a multichannel run, more than two output channels
    carry signal in at least one probed configuration; in a 2-channel run,
    both channels do. Exit code 1 (FAIL): every configuration leaves only the
    front stereo pair carrying signal - the "only the front speakers play"
    failure users reported. Exit code 2: setup error (bad arguments, plugin
    failed to load).
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "filters/VSTPluginFilter.h"

namespace
{
constexpr float probeSampleRate = 48000.0f;
constexpr unsigned probeBlockSize = 512;
// Long warm-up so upmixer latency (the plugin advertises up to 40 ms quality
// delay) and any decorrelator ramp-in are over before measuring.
constexpr unsigned defaultWarmupSeconds = 8;
constexpr unsigned attemptWarmupSeconds = 4;
constexpr unsigned measureSeconds = 2;
constexpr unsigned maxConfigurationAttempts = 12;
constexpr int maxSelectorSteps = 15;
constexpr double activeRmsThreshold = 1.0e-6;
constexpr double pi = 3.14159265358979323846;

const wchar_t* wellKnownChannelName(unsigned index, unsigned count)
{
	if (count == 8)
	{
		static const wchar_t* surround71[8] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
		return surround71[index];
	}
	if (count == 6)
	{
		static const wchar_t* surround51[6] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
		return surround51[index];
	}
	if (count == 2)
		return index == 0 ? L"L" : L"R";
	return nullptr;
}

std::wstring toLower(const std::wstring& text)
{
	std::wstring result = text;
	for (wchar_t& c : result)
		c = towlower(c);
	return result;
}

struct RunResult
{
	unsigned activeChannels = 0;
	std::vector<double> rms;
	std::vector<double> peaks;
};

// Runs a stereo test signal (a correlated component an upmixer steers to the
// center plus an anti-correlated component it steers to the surrounds; all
// other input channels silent) through a fresh VSTPluginFilter and measures
// the level every output channel carries once the warm-up is over.
RunResult runProbe(const std::shared_ptr<VSTPluginLibrary>& library,
	const std::vector<std::wstring>& channelNames,
	const std::unordered_map<std::wstring, float>& paramMap,
	unsigned warmupSeconds, bool printTable)
{
	const unsigned channelCount = static_cast<unsigned>(channelNames.size());
	VSTPluginFilter filter(library, std::wstring(), paramMap);
	filter.initialize(probeSampleRate, probeBlockSize, channelNames);

	std::vector<std::vector<double>> inputData(channelCount, std::vector<double>(probeBlockSize, 0.0));
	std::vector<std::vector<double>> outputData(channelCount, std::vector<double>(probeBlockSize, 0.0));
	std::vector<double*> inputs(channelCount);
	std::vector<double*> outputs(channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		inputs[i] = inputData[i].data();
		outputs[i] = outputData[i].data();
	}

	RunResult result;
	result.rms.assign(channelCount, 0.0);
	result.peaks.assign(channelCount, 0.0);
	std::vector<double> sumSquares(channelCount, 0.0);
	const unsigned totalBlocks = static_cast<unsigned>((warmupSeconds + measureSeconds) * probeSampleRate) / probeBlockSize;
	const unsigned measureStartBlock = static_cast<unsigned>(warmupSeconds * probeSampleRate) / probeBlockSize;
	unsigned long long measuredSamples = 0;
	unsigned long long samplePosition = 0;

	for (unsigned block = 0; block < totalBlocks; block++)
	{
		for (unsigned s = 0; s < probeBlockSize; s++)
		{
			const double t = static_cast<double>(samplePosition + s) / probeSampleRate;
			const double common = 0.25 * sin(2.0 * pi * 997.0 * t);
			const double side = 0.1 * sin(2.0 * pi * 3163.0 * t);
			inputData[0][s] = common + side;
			inputData[1][s] = common - side;
		}
		samplePosition += probeBlockSize;
		filter.process(outputs.data(), inputs.data(), probeBlockSize);
		if (block < measureStartBlock)
			continue;
		measuredSamples += probeBlockSize;
		for (unsigned c = 0; c < channelCount; c++)
		{
			for (unsigned s = 0; s < probeBlockSize; s++)
			{
				const double v = outputData[c][s];
				sumSquares[c] += v * v;
				if (fabs(v) > result.peaks[c])
					result.peaks[c] = fabs(v);
			}
		}
	}

	for (unsigned c = 0; c < channelCount; c++)
	{
		result.rms[c] = measuredSamples > 0 ? sqrt(sumSquares[c] / static_cast<double>(measuredSamples)) : 0.0;
		if (result.rms[c] > activeRmsThreshold)
			result.activeChannels++;
	}

	if (printTable)
	{
		wprintf(L"per-channel output over the last %u s of a %u s stereo run (%u channels):\n",
			measureSeconds, warmupSeconds + measureSeconds, channelCount);
		for (unsigned c = 0; c < channelCount; c++)
		{
			const double rmsDb = result.rms[c] > 0.0 ? 20.0 * log10(result.rms[c]) : -999.0;
			wprintf(L"  %-4s rms=%.8f (%7.1f dBFS)  peak=%.8f  %s\n",
				channelNames[c].c_str(), result.rms[c], rmsDb, result.peaks[c],
				result.rms[c] > activeRmsThreshold ? L"ACTIVE" : L"silent");
		}
		wprintf(L"active channels: %u / %u\n", result.activeChannels, channelCount);
	}
	return result;
}

struct ConfigurationCandidate
{
	std::wstring title;
	double normalizedValue = 0.0;
	std::wstring valueString;
	int score = 0;
};
}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 3)
	{
		fwprintf(stderr, L"usage: RealVstProbe.exe <plugin path> <channel count>\n");
		return 2;
	}
	const std::wstring pluginPath = argv[1];
	const int channelCountArg = _wtoi(argv[2]);
	if (channelCountArg < 2 || channelCountArg > 32)
	{
		fwprintf(stderr, L"channel count must be between 2 and 32\n");
		return 2;
	}
	const unsigned channelCount = static_cast<unsigned>(channelCountArg);

	std::shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(pluginPath);
	if (library == NULL || library->initialize() < 0)
	{
		fwprintf(stderr, L"FAIL: could not load/initialize plugin library %s\n", pluginPath.c_str());
		return 2;
	}
	wprintf(L"plugin: %s\n", pluginPath.c_str());
	wprintf(L"format: %s\n", library->isVST3() ? L"VST3" : L"VST2");

	// Instance-level negotiation record plus the parameter inventory the
	// configuration search below draws from.
	std::vector<ConfigurationCandidate> candidates;
	{
		VSTPluginInstance probe(library, 2);
		if (!probe.initialize())
		{
			fwprintf(stderr, L"FAIL: plugin component did not initialize\n");
			return 2;
		}
		wprintf(L"name: %s\n", probe.getName().c_str());
		wprintf(L"after stereo probe:          in=%d out=%d\n", probe.numInputs(), probe.numOutputs());
		const bool negotiated = probe.negotiateChannelCount(static_cast<int>(channelCount));
		wprintf(L"negotiateChannelCount(%2u):   in=%d out=%d (%s)\n", channelCount,
			probe.numInputs(), probe.numOutputs(), negotiated ? L"accepted" : L"rejected");

		const int parameterCount = probe.getParameterCount();
		wprintf(L"\nparameters (%d):\n", parameterCount);
		for (int i = 0; i < parameterCount; i++)
		{
			std::wstring title;
			double value = 0.0;
			int stepCount = 0;
			if (!probe.getParameterDetails(i, title, value, stepCount))
				continue;
			wprintf(L"  [%2d] %-24s steps=%-3d value=%.3f (%s)\n", i, title.c_str(), stepCount, value,
				probe.getParameterValueString(i, value).c_str());
			if (stepCount < 1 || stepCount > maxSelectorSteps)
				continue;
			const std::wstring lowerTitle = toLower(title);
			const bool outputLikeTitle = lowerTitle.find(L"out") != std::wstring::npos
				|| lowerTitle.find(L"mode") != std::wstring::npos
				|| lowerTitle.find(L"channel") != std::wstring::npos
				|| lowerTitle.find(L"speaker") != std::wstring::npos
				|| lowerTitle.find(L"layout") != std::wstring::npos;
			for (int k = 0; k <= stepCount; k++)
			{
				const double stepValue = static_cast<double>(k) / stepCount;
				const std::wstring stepString = probe.getParameterValueString(i, stepValue);
				wprintf(L"        step %2d -> %s\n", k, stepString.c_str());
				// Prefer a native 7.1 layout for the 8-channel bus, then
				// 7.1.4 (its bed still spans all 8 channels), then 5.1.
				int score = 0;
				if (stepString.find(L"7.1.4") != std::wstring::npos)
					score = 2;
				else if (stepString.find(L"7.1") != std::wstring::npos)
					score = 3;
				else if (stepString.find(L"5.1") != std::wstring::npos)
					score = 1;
				if (score == 0 && outputLikeTitle)
					score = 1;
				if (score > 0)
					candidates.push_back({title, stepValue, stepString, score});
			}
		}
	}

	std::vector<std::wstring> channelNames;
	for (unsigned i = 0; i < channelCount; i++)
	{
		const wchar_t* known = wellKnownChannelName(i, channelCount);
		channelNames.push_back(known != nullptr ? std::wstring(known) : (L"CH" + std::to_wstring(i + 1)));
	}

	wprintf(L"\n=== default plugin state ===\n");
	const RunResult defaultResult = runProbe(library, channelNames,
		std::unordered_map<std::wstring, float>(), defaultWarmupSeconds, true);

	bool pass = channelCount <= 2 ? defaultResult.activeChannels == channelCount
		: defaultResult.activeChannels > 2;
	std::wstring passingConfiguration = L"factory default state";

	if (!pass && channelCount > 2 && !candidates.empty())
	{
		// Highest-priority layouts first; drop the rest once the cap is hit.
		std::stable_sort(candidates.begin(), candidates.end(),
			[](const ConfigurationCandidate& a, const ConfigurationCandidate& b) { return a.score > b.score; });
		if (candidates.size() > maxConfigurationAttempts)
			candidates.resize(maxConfigurationAttempts);

		wprintf(L"\n=== configuration search (%u candidates) ===\n", static_cast<unsigned>(candidates.size()));
		for (const ConfigurationCandidate& candidate : candidates)
		{
			std::unordered_map<std::wstring, float> paramMap;
			paramMap[candidate.title] = static_cast<float>(candidate.normalizedValue);
			const RunResult attempt = runProbe(library, channelNames, paramMap, attemptWarmupSeconds, false);
			wprintf(L"  %s = %s (normalized %.3f) -> active %u/%u\n",
				candidate.title.c_str(), candidate.valueString.c_str(), candidate.normalizedValue,
				attempt.activeChannels, channelCount);
			if (attempt.activeChannels > 2)
			{
				pass = true;
				passingConfiguration = candidate.title + L" = " + candidate.valueString;
				wprintf(L"\n=== passing configuration: %s ===\n", passingConfiguration.c_str());
				runProbe(library, channelNames, paramMap, defaultWarmupSeconds, true);
				break;
			}
		}
	}

	wprintf(L"\nverdict: %s\n", pass
		? (std::wstring(L"PASS (") + passingConfiguration + L")").c_str()
		: L"FAIL (only the front stereo pair carries signal in every probed configuration)");
	return pass ? 0 : 1;
}
