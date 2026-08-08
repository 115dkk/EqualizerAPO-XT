#pragma once

#include <vector>

#include "engine/IFilter.h"
#include "filters/HilbertCommand.h"
#include "filters/IrCache.h"

constexpr unsigned HilbertTapCount = 1025;
constexpr unsigned HilbertLatencySamples = (HilbertTapCount - 1) / 2;

// Exposed for deterministic command/DSP tests and for the Editor readout.
std::vector<double> designHilbertFir(int directionDegrees);

#pragma AVRT_VTABLES_BEGIN
class HilbertFilter : public IFilter
{
public:
	explicit HilbertFilter(const HilbertCommand& command);
	~HilbertFilter();
	// The deferred mute diagnostic's prefix is part of the filter's
	// observable contract (HybridConvTests pins it), like the other
	// convolvers' (audit #250 A4 - Hilbert used to mute silently).
	static constexpr const wchar_t* kFrameCountMismatchLogPrefix =
		L"HilbertFilter: frameCount";
	bool getAllChannels() override { return true; }
	bool getInPlace() override { return false; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	HilbertCommand command;
	std::vector<double> coefficients;
	HConvSingleArray filters;
	std::vector<int> shifted;
	std::vector<int> aligned;
	std::vector<std::vector<double>> delayLines;
	unsigned delayOffset = 0;
	unsigned channelCount = 0;
	unsigned filterFrameCount = 0;
	bool frameCountMismatchLogged = false;
};
#pragma AVRT_VTABLES_END
