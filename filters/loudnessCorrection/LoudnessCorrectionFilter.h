/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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

#include <IFilter.h>
#include <filters/BiQuad.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>

#pragma AVRT_VTABLES_BEGIN
class LoudnessCorrectionFilter : public IFilter
{
public:
	// Runtime parameter set for the filter. Parsing and serialization of the
	// "LoudnessCorrection:" config line live in LoudnessCorrectionCommand.
	struct FilterParameters
	{
		bool state = false;
		float referenceLevel = 0.0f;
		float referenceOffset = 0.0f;
		float attenuation = 1.0f;
	};

	LoudnessCorrectionFilter(const FilterParameters& fParameters);
	virtual ~LoudnessCorrectionFilter();
	virtual bool getInPlace() {return true;}
	virtual std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames);
	virtual void process(double** output, double** input, unsigned frameCount);

private:
	void getLShelfParamter(const double& volume, double& frequence, double& q, double& gain, double& preAmp);
	void getHShelfParamter(const double& volume, double& frequence, double& q, double& gain);
	void upDateBiquadCoefficients(const double& freq, const double& bandwidthOrQOrS, const double& dbGain, bool highshelf);
	bool upDateNeutral();

	std::thread _parameterUpdateThread;
	static void parameterUpdateThread(LoudnessCorrectionFilter* filter);
	std::mutex _parameterUpdateMutex;
	std::mutex _parameterUpdateThreadMutex;
	std::condition_variable _parameterUpdateThreadCv;
	bool _stopParameterUpdateThread = false;
	std::atomic_bool _parameterChanged = false;

	FilterParameters _parameters;
	size_t _channelCount = 0;
	float _sampleRate = 0.0f;
	double _attFactor = 1.0;
	double _pendingAttFactor = 1.0;
	std::vector<BiQuad> _lowShelfBiquads;
	std::vector<BiQuad> _highShelfBiquads;

	bool _neutral = true;
	bool _neutralUpDate = true;
	double _tempResult = 0.0;
	double _aLS[4] = {};
	double _a0LS = 0.0;
	double _aHS[4] = {};
	double _a0HS = 0.0;
};
#pragma AVRT_VTABLES_END
