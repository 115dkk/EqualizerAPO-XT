#include "stdafx.h"
#include "HilbertFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <set>

#include "helpers/ChannelHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/PerfProfile.h"

namespace
{
constexpr double Pi = 3.1415926535897932384626433832795;

double besselI0(double value)
{
	double sum = 1.0;
	double term = 1.0;
	const double quarter = value * value / 4.0;
	for (int k = 1; k < 32; ++k)
	{
		term *= quarter / static_cast<double>(k * k);
		sum += term;
		if (term < sum * 1e-16)
			break;
	}
	return sum;
}

std::vector<int> resolve(const std::vector<std::wstring>& requested,
	const std::vector<std::wstring>& channelNames, bool allowAll)
{
	if (allowAll && requested.size() == 1 && requested.front() == L"ALL")
	{
		std::vector<int> all(channelNames.size());
		for (size_t i = 0; i < channelNames.size(); ++i)
			all[i] = static_cast<int>(i);
		return all;
	}
	std::vector<int> result;
	for (const std::wstring& name : requested)
	{
		const int index = ChannelHelper::getChannelIndex(name, channelNames, true);
		if (index >= 0
			&& std::find(result.begin(), result.end(), index) == result.end())
			result.push_back(index);
		else if (index < 0)
			LogFStatic(L"Hilbert: channel %s is not available; ignoring it",
				name.c_str());
	}
	return result;
}
}

std::vector<double> designHilbertFir(int directionDegrees)
{
	std::vector<double> taps(HilbertTapCount, 0.0);
	constexpr int midpoint = static_cast<int>(HilbertLatencySamples);
	constexpr double beta = 8.6;
	const double denominator = besselI0(beta);
	const double sign = directionDegrees < 0 ? 1.0 : -1.0;
	for (int n = 0; n < static_cast<int>(HilbertTapCount); ++n)
	{
		const int k = n - midpoint;
		if (k == 0 || (std::abs(k) & 1) == 0)
			continue;
		const double position = static_cast<double>(k) / midpoint;
		const double window = besselI0(beta
			* std::sqrt(std::max(0.0, 1.0 - position * position)))
			/ denominator;
		taps[n] = sign * 2.0 * window / (Pi * static_cast<double>(k));
	}

	// Pin the mid-band gain to unity. Windowing changes it by a small amount;
	// normalizing here makes the card's 0 dB passband readout exact.
	std::complex<double> response;
	for (size_t n = 0; n < taps.size(); ++n)
		response += taps[n] * std::exp(std::complex<double>(
			0.0, -Pi * 0.5 * static_cast<double>(n)));
	const double gain = std::abs(response);
	if (gain > 0.0)
		for (double& tap : taps)
			tap /= gain;
	return taps;
}

HilbertFilter::HilbertFilter(const HilbertCommand& command)
	: command(command)
{
}

std::vector<std::wstring> HilbertFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	(void)sampleRate;
	filters = nullptr;
	channelCount = static_cast<unsigned>(channelNames.size());
	filterFrameCount = 0;
	delayOffset = 0;
	shifted = resolve(command.shiftedChannels, channelNames, true);
	aligned = resolve(command.alignedChannels, channelNames, false);

	// Aliases can resolve two different spellings to one physical channel.
	// The phase-shifted role wins and the duplicate aligned role is dropped.
	aligned.erase(std::remove_if(aligned.begin(), aligned.end(),
		[this](int index) {
			return std::find(shifted.begin(), shifted.end(), index) != shifted.end();
		}), aligned.end());

	coefficients = designHilbertFir(command.directionDegrees);
	if (!shifted.empty())
	{
		std::vector<ConvolverUnitSource> sources(shifted.size());
		for (size_t i = 0; i < sources.size(); ++i)
			sources[i] = {coefficients.data(),
				static_cast<unsigned>(coefficients.size()), 0};
		filters = buildConvolverArray(sources, maxFrameCount);
		if (filters != nullptr)
			filterFrameCount = maxFrameCount;
	}

	delayLines.assign(channelCount, {});
	for (int channel : aligned)
		delayLines[static_cast<size_t>(channel)].assign(
			HilbertLatencySamples, 0.0);
	TraceF(L"Hilbert %d degrees: %zu shifted, %zu aligned, %u taps",
		command.directionDegrees, shifted.size(), aligned.size(), HilbertTapCount);
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void HilbertFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("HilbertFilter::process");
	for (unsigned channel = 0; channel < channelCount; ++channel)
		if (output[channel] != input[channel])
			std::copy_n(input[channel], frameCount, output[channel]);

	const bool convolverReady = filters != nullptr
		&& frameCount == filterFrameCount;
	for (size_t unit = 0; unit < shifted.size(); ++unit)
	{
		double* out = output[shifted[unit]];
		if (!convolverReady)
		{
			std::fill_n(out, frameCount, 0.0);
			continue;
		}
		hcPutSingle(&filters[static_cast<unsigned>(unit)], input[shifted[unit]]);
		hcProcessSingle(&filters[static_cast<unsigned>(unit)]);
		hcGetSingle(&filters[static_cast<unsigned>(unit)], out);
	}

	for (unsigned frame = 0; frame < frameCount; ++frame)
	{
		for (int channel : aligned)
		{
			std::vector<double>& line = delayLines[static_cast<size_t>(channel)];
			output[channel][frame] = line[delayOffset];
			line[delayOffset] = input[channel][frame];
		}
		if (!aligned.empty())
			delayOffset = (delayOffset + 1) % HilbertLatencySamples;
	}
}
#pragma AVRT_CODE_END
