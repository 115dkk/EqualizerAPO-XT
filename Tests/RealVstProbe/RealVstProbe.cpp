/*
    RealVstProbe - one-off engineering-evidence harness.

    Loads a real third-party VST3 plugin by path through the engine's public
    host classes (VSTPluginLibrary + VSTPluginFilter), simulates an N-channel
    Equalizer APO device playing a stereo music signal, and reports the level
    every output channel actually carries. This exercises the same code path
    the APO runs on a real machine, so a channel that stays silent here stays
    silent on that machine's speaker as well.

    Usage: RealVstProbe.exe <plugin path> <channel count>

    Exit code 0 (PASS): in a multichannel run, more than two output channels
    carry signal; in a 2-channel run, both do. Exit code 1 (FAIL): only the
    front stereo pair carries signal - the "only the front speakers play"
    failure users reported. Exit code 2: setup error (bad arguments, plugin
    failed to load).
*/

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
constexpr unsigned warmupSeconds = 8;
constexpr unsigned measureSeconds = 2;
constexpr double activeRmsThreshold = 1.0e-6;
constexpr double pi = 3.14159265358979323846;

const wchar_t* wellKnownChannelName(unsigned index, unsigned count)
{
	static const wchar_t* surround71[8] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
	static const wchar_t* surround51[6] = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
	if (count == 8)
		return surround71[index];
	if (count == 6)
		return surround51[index];
	if (count == 2)
		return index == 0 ? L"L" : L"R";
	return nullptr;
}
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

	// Instance-level negotiation record: what the plugin reports after the
	// stereo probe, and what it accepts when offered the device width.
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
	}

	std::vector<std::wstring> channelNames;
	for (unsigned i = 0; i < channelCount; i++)
	{
		const wchar_t* known = wellKnownChannelName(i, channelCount);
		channelNames.push_back(known != nullptr ? std::wstring(known) : (L"CH" + std::to_wstring(i + 1)));
	}

	VSTPluginFilter filter(library, std::wstring(), std::unordered_map<std::wstring, float>());
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

	// Stereo program material on L/R only: a correlated component (which an
	// upmixer steers to the center) plus an anti-correlated component (which
	// it steers to the surrounds). All other input channels stay silent, per
	// the plugin author's stated contract.
	std::vector<double> sumSquares(channelCount, 0.0);
	std::vector<double> peaks(channelCount, 0.0);
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
				if (fabs(v) > peaks[c])
					peaks[c] = fabs(v);
			}
		}
	}

	wprintf(L"\nper-channel output over the last %u s of a %u s stereo run (%u channels):\n",
		measureSeconds, warmupSeconds + measureSeconds, channelCount);
	unsigned activeChannels = 0;
	for (unsigned c = 0; c < channelCount; c++)
	{
		const double rms = measuredSamples > 0 ? sqrt(sumSquares[c] / static_cast<double>(measuredSamples)) : 0.0;
		const bool active = rms > activeRmsThreshold;
		if (active)
			activeChannels++;
		const double rmsDb = rms > 0.0 ? 20.0 * log10(rms) : -999.0;
		wprintf(L"  %-4s rms=%.8f (%7.1f dBFS)  peak=%.8f  %s\n",
			channelNames[c].c_str(), rms, rmsDb, peaks[c], active ? L"ACTIVE" : L"silent");
	}
	wprintf(L"active channels: %u / %u\n", activeChannels, channelCount);

	const bool pass = channelCount <= 2 ? activeChannels == channelCount : activeChannels > 2;
	wprintf(L"verdict: %s\n", pass ? L"PASS" : L"FAIL (only the front stereo pair carries signal)");
	return pass ? 0 : 1;
}
