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

#include <memory>
#include <string>
#include <vector>

#include "IFilter.h"

class FilterEngine;

struct FilterDeleter
{
	void operator()(IFilter* filter) const;
};

struct FilterInfo
{
	std::unique_ptr<IFilter, FilterDeleter> filter;
	bool inPlace = true;
	std::vector<size_t> inChannels;
	std::vector<size_t> outChannels;
};

#pragma AVRT_VTABLES_BEGIN
class FilterConfiguration
{
public:
	FilterConfiguration(FilterEngine* engine, std::vector<std::unique_ptr<FilterInfo>> filterInfos, unsigned allChannelCount);
	~FilterConfiguration();

	void read(double* input, unsigned frameCount);
	void read(double** input, unsigned frameCount);
	void process(unsigned frameCount);
	unsigned doTransition(FilterConfiguration* nextConfig, unsigned frameCount, unsigned transitionCounter, unsigned transitionLength);
	void write(double* output, unsigned frameCount);
	void write(double** output, unsigned frameCount);
	double** getOutputSamples() {return allSamples.data();}
	bool isEmpty();

private:
	unsigned realChannelCount;
	unsigned outputChannelCount;
	unsigned allChannelCount;
	std::vector<double> allSamplesData;
	std::vector<double> allSamples2Data;
	std::vector<double*> allSamples;
	std::vector<double*> allSamples2;
	std::vector<double*> currentSamples;
	std::vector<double*> currentSamples2;
	std::vector<std::unique_ptr<FilterInfo>> filterInfos;
};
#pragma AVRT_VTABLES_END
