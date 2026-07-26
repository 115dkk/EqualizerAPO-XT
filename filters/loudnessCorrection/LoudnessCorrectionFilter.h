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

#include <engine/IFilter.h>
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
	// Complete coefficient set handed from the parameter update thread to the
	// audio thread. Every field is rewritten on each publication, so a slot
	// never mixes values from two different volume readings.
	struct CoefficientSet
	{
		double attFactor = 1.0;
		double aLS[4] = {};
		double a0LS = 0.0;
		double aHS[4] = {};
		double a0HS = 0.0;
		bool neutral = true;
	};

	void getLShelfParamter(const double& volume, double& frequence, double& q, double& gain, double& preAmp);
	void getHShelfParamter(const double& volume, double& frequence, double& q, double& gain);
	void upDateBiquadCoefficients(CoefficientSet& target, const double& freq, const double& bandwidthOrQOrS, const double& dbGain, bool highshelf);

	std::thread _parameterUpdateThread;
	static void parameterUpdateThread(LoudnessCorrectionFilter* filter);
	// Guards the stop flag and the 10 ms sleep of the parameter update thread
	// only. The audio thread never touches this mutex; do not reuse it for the
	// coefficient hand-off, which is what _coefficientSlots is for.
	std::mutex _parameterUpdateThreadMutex;
	std::condition_variable _parameterUpdateThreadCv;
	bool _stopParameterUpdateThread = false;
	std::atomic_bool _parameterChanged = false;

	// Double-buffered hand-off, published with a release store and read with an
	// acquire load, so the audio thread never blocks on a thread that calls COM
	// through VolumeController.
	//
	// Two slots are provably enough, not merely likely enough. The update thread
	// only starts a new set once it observes _parameterChanged == false, and only
	// process() clears that flag. So between two consecutive writes to the same
	// slot there must be two clears, i.e. two process() calls. process() finishes
	// copying a slot before it returns, and the audio thread calls process()
	// sequentially, so the write to slot N+2 cannot start before the read of slot
	// N has finished. A single buffer would not be safe: process() clears the flag
	// before it copies, which already lets the update thread start the next set.
	CoefficientSet _coefficientSlots[2];
	std::atomic<unsigned> _publishedSlot = 0;

	FilterParameters _parameters;
	size_t _channelCount = 0;
	float _sampleRate = 0.0f;
	double _attFactor = 1.0;
	std::vector<BiQuad> _lowShelfBiquads;
	std::vector<BiQuad> _highShelfBiquads;

	bool _neutral = true;
	double _tempResult = 0.0;
};
#pragma AVRT_VTABLES_END
