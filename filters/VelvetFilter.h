/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "engine/IFilter.h"
#include "filters/velvet/VelvetProcessor.h"

#pragma AVRT_VTABLES_BEGIN
class VelvetFilter : public IFilter
{
public:
	explicit VelvetFilter(const velvet::Parameters& parameters);
	bool getInPlace() override { return false; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	const velvet::Parameters& getParameters() const { return parameters; }
	const velvet::Statistics& getStatistics() const { return statistics; }

private:
	velvet::Parameters parameters;
	velvet::Processor processor;
	velvet::Statistics statistics;
	std::vector<const double*> inputPointers;
	unsigned channelCount = 0;
	bool prepared = false;
};
#pragma AVRT_VTABLES_END
