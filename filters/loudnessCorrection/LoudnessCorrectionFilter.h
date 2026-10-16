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
#include <sstream>
#include <thread>

#include "platform/windows/Win32Event.h"

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

	// The low-shelf stage for one volume reading: the boost it applies and
	// the preamp that makes room for it. Pure, so the three volume regions
	// (below, at and above the reference point) can be tested directly.
	struct LowShelf
	{
		double frequency = 75;
		double q = 0.52;
		double gain = 0.0;
		double preAmp = 0.0;
	};
	static LowShelf lowShelfFor(const FilterParameters& parameters, double volume);
	struct HighShelf
	{
		double frequency = 10000;
		double q = 0.9;
		double gain = 0.0;
	};
	static HighShelf highShelfFor(const FilterParameters& parameters, double volume);

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

	void publishCoefficientSet(double volume);
	void upDateBiquadCoefficients(CoefficientSet& target, const double& freq, const double& bandwidthOrQOrS, const double& dbGain, bool highshelf);

	std::thread _parameterUpdateThread;
	static void parameterUpdateThread(LoudnessCorrectionFilter* filter);
	// Stops and joins the notification waiter if one runs; the destructor and a
	// repeated initialize() both need it.
	void stopParameterUpdateThread();
	Win32Event _parameterUpdateStopEvent{true, false};
	Win32Event _coefficientConsumedEvent{false, false};
	std::atomic_bool _parameterChanged = false;

	// Double-buffered hand-off, published with a release store and read with an
	// acquire load, so the audio thread never blocks on a thread that calls COM
	// through VolumeController.
	//
	// Two slots are enough because the notification waiter publishes one set and
	// then waits for _coefficientConsumedEvent. process() signals that event only
	// after copying the published slot, so the waiter cannot write slot N+2 while
	// the audio thread still reads slot N.
	CoefficientSet _coefficientSlots[2];
	std::atomic<unsigned> _publishedSlot = 0;

	FilterParameters _parameters;
	double _initialVolume = 0.0;
	size_t _channelCount = 0;
	float _sampleRate = 0.0f;
	double _attFactor = 1.0;
	std::vector<BiQuad> _lowShelfBiquads;
	std::vector<BiQuad> _highShelfBiquads;

	bool _neutral = true;
	double _tempResult = 0.0;
};
#pragma AVRT_VTABLES_END
