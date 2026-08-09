#include "stdafx.h"
#include "VelvetFilter.h"

#include <algorithm>

#include "services/logging/LogHelper.h"
#include "diagnostics/performance/PerfProfile.h"

VelvetFilter::VelvetFilter(const velvet::Parameters& parameters)
	: parameters(parameters)
{
}

std::vector<std::wstring> VelvetFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	(void)maxFrameCount;
	channelCount = static_cast<unsigned>(channelNames.size());
	inputPointers.assign(channelCount, nullptr);
	prepared = processor.prepare(sampleRate, channelCount)
		&& processor.setParameters(parameters);
	if (!prepared)
	{
		LogFStatic(L"Velvet: failed to prepare %u channels at %.0f Hz; passing audio through",
			channelCount, sampleRate);
	}
	else
	{
		statistics = processor.statistics();
		TraceF(L"Velvet %s: %zu taps/channel, %.3f ms, max zero-lag correlation %.4f",
			parameters.dynamic ? L"dynamic" : L"static",
			statistics.tapsPerChannel, parameters.lengthMs,
			statistics.maximumZeroLagCorrelation);
	}
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void VelvetFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("VelvetFilter::process");
	if (channelCount == 0)
		return;
	if (!prepared)
	{
		for (unsigned channel = 0; channel < channelCount; ++channel)
		{
			if (output[channel] != input[channel])
				std::copy(input[channel], input[channel] + frameCount, output[channel]);
		}
		return;
	}
	for (unsigned channel = 0; channel < channelCount; ++channel)
		inputPointers[channel] = input[channel];
	processor.process(output, inputPointers.data(), frameCount);
}
#pragma AVRT_CODE_END
