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

#include "stdafx.h"
#include "LoudnessCorrectionFilter.h"

#include "VolumeController.h"

#include <math.h>
#include <numbers>

LoudnessCorrectionFilter::LoudnessCorrectionFilter(const FilterParameters& fParameters)
	: _parameters(fParameters)
{
	if (_parameters.attenuation > 1.0)
	{
		_parameters.attenuation = 1.0;
	}
	if (_parameters.attenuation < 0.0)
	{
		_parameters.attenuation = 0.0;
	}
}

LoudnessCorrectionFilter::~LoudnessCorrectionFilter()
{
	stopParameterUpdateThread();
}

void LoudnessCorrectionFilter::stopParameterUpdateThread()
{
	_parameterUpdateStopEvent.set();
	if (_parameterUpdateThread.joinable())
		_parameterUpdateThread.join();
}

std::vector<std::wstring> LoudnessCorrectionFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	// A second call re-initializes. The update thread from the first call is
	// stopped first: assigning a new std::thread over a joinable one is
	// std::terminate, and the thread would otherwise race the slot seeding
	// below (audit #348 open question, IFilter::initialize contract).
	stopParameterUpdateThread();

	this->_channelCount = channelNames.size();
	_lowShelfBiquads.resize(_channelCount);
	_highShelfBiquads.resize(_channelCount);
	_sampleRate = sampleRate;
	_attFactor = 1.0;
	_neutral = true;

	LowShelf lowShelf;
	lowShelf.q = 1.0;
	HighShelf highShelf;
	highShelf.q = 1.0;
	_initialVolume = _parameters.referenceLevel;
	VolumeController volumeController;
	if (volumeController.getVolume(_initialVolume) == S_OK)
	{
		lowShelf = lowShelfFor(_parameters, _initialVolume);
		highShelf = highShelfFor(_parameters, _initialVolume + lowShelf.preAmp);
		_attFactor = exp(lowShelf.preAmp / 6 * log(2));
		_neutral = (std::max)(std::abs(lowShelf.gain), std::abs(highShelf.gain)) < 0.2;
	}

	for (unsigned i = 0; i < _channelCount; i++)
	{
		_lowShelfBiquads[i] = BiQuad(BiQuad::LOW_SHELF, lowShelf.gain,
			lowShelf.frequency, _sampleRate, lowShelf.q, false);
		_highShelfBiquads[i] = BiQuad(BiQuad::HIGH_SHELF, highShelf.gain,
			highShelf.frequency, _sampleRate, highShelf.q, false);
	}

	// Seed both slots before the update thread starts. process() only copies a
	// slot after the update thread published a full set, so these values never
	// reach the audio, but they keep every slot readable at any time.
	CoefficientSet seed;
	seed.attFactor = _attFactor;
	seed.neutral = _neutral;
	_coefficientSlots[0] = seed;
	_coefficientSlots[1] = seed;
	_publishedSlot.store(0, std::memory_order_relaxed);

	_parameterUpdateStopEvent.reset();
	_coefficientConsumedEvent.reset();
	_parameterChanged.store(false);
	_parameterUpdateThread = std::thread(parameterUpdateThread, this);

	return channelNames;
}

LoudnessCorrectionFilter::LowShelf LoudnessCorrectionFilter::lowShelfFor(const FilterParameters& parameters, double volume)
{
	LowShelf shelf;
	const double volDiff = parameters.referenceLevel - parameters.referenceOffset - volume;
	if (volDiff > 0)
	{
		// old: gain=volDiff*0.55*_parameters.attenuation;
		shelf.gain = volDiff * 0.55 / (1 - 0.55) * parameters.attenuation;
		shelf.preAmp = -shelf.gain;
	}
	else if (volDiff < 0)
	{
		shelf.gain = volDiff * 0.55 * exp(volDiff / 90.0) * parameters.attenuation;
	}
	// At the reference point both stay 0. The out-parameter version this
	// replaced left preAmp unwritten there, so initialize() computed the
	// attenuation from an uninitialised double (audit #348 TD-03).
	return shelf;
}

LoudnessCorrectionFilter::HighShelf LoudnessCorrectionFilter::highShelfFor(
	const FilterParameters& parameters, double volume)
{
	HighShelf shelf;
	const double volDiff = parameters.referenceLevel - parameters.referenceOffset - volume;
	if (volDiff > 0)
		shelf.gain = volDiff * 0.225 * exp(-volDiff / 100.0) * parameters.attenuation;
	else if (volDiff < 0)
		shelf.gain = volDiff * 0.175 * exp(volDiff / 80.0) * parameters.attenuation;
	return shelf;
}

void LoudnessCorrectionFilter::publishCoefficientSet(double volume)
{
	const LowShelf lowShelf = lowShelfFor(_parameters, volume);
	const HighShelf highShelf = highShelfFor(_parameters, volume + lowShelf.preAmp);

	// Fill the slot process() is not reading. The notification waiter does not
	// publish again until process() signals that it copied the previous slot.
	const unsigned nextSlot = 1u - _publishedSlot.load(std::memory_order_relaxed);
	CoefficientSet& target = _coefficientSlots[nextSlot];
	target.attFactor = exp(lowShelf.preAmp / 6 * log(2));
	upDateBiquadCoefficients(target, highShelf.frequency, highShelf.q, highShelf.gain, true);
	upDateBiquadCoefficients(target, lowShelf.frequency, lowShelf.q, lowShelf.gain, false);
	target.neutral = (std::max)(std::abs(lowShelf.gain), std::abs(highShelf.gain)) < 0.2;
	_publishedSlot.store(nextSlot, std::memory_order_release);
	_parameterChanged.store(true, std::memory_order_release);
}

void LoudnessCorrectionFilter::parameterUpdateThread(LoudnessCorrectionFilter* lCorrection)
{
	VolumeController volumeController;
	if (FAILED(volumeController.startNotifications()))
		return;

	double previousVolume = lCorrection->_initialVolume;
	double currentVolume = previousVolume;
	if (volumeController.getVolume(currentVolume) == S_OK && currentVolume != previousVolume)
	{
		lCorrection->publishCoefficientSet(currentVolume);
		previousVolume = currentVolume;
	}

	HANDLE changeHandles[2] = {
		lCorrection->_parameterUpdateStopEvent.get(),
		volumeController.notificationHandle()
	};
	HANDLE consumedHandles[2] = {
		lCorrection->_parameterUpdateStopEvent.get(),
		lCorrection->_coefficientConsumedEvent.get()
	};
	while (true)
	{
		const DWORD changed = Win32Event::waitAny(2, changeHandles);
		if (changed == WAIT_OBJECT_0 || changed == WAIT_FAILED)
			break;
		if (changed != WAIT_OBJECT_0 + 1)
			continue;

		while (lCorrection->_parameterChanged.load(std::memory_order_acquire))
		{
			const DWORD consumed = Win32Event::waitAny(2, consumedHandles);
			if (consumed == WAIT_OBJECT_0 || consumed == WAIT_FAILED)
				return;
		}

		// Coalesce notifications that arrived while the previous coefficient set
		// was waiting for the audio thread by reading the endpoint once now.
		if (volumeController.getVolume(currentVolume) == S_OK
			&& currentVolume != previousVolume)
		{
			lCorrection->publishCoefficientSet(currentVolume);
			previousVolume = currentVolume;
		}
	}
}

#pragma AVRT_CODE_BEGIN
void LoudnessCorrectionFilter::process(double** output, double** input, unsigned frameCount)
{
	if (_parameters.state == false)
	{
		for (unsigned int j = 0; j < frameCount; j++)
		{
			for (unsigned i = 0; i < _channelCount; i++)
			{
				output[i][j] = input[i][j];
			}
		}
		return;
	}
	if (_parameterChanged.exchange(false))
	{
		// Acquire: pairs with the notification waiter's release publication and
		// makes that slot's coefficients visible here. No lock is taken on the
		// audio thread; the consumed event is signalled after the copy.
		const CoefficientSet& published = _coefficientSlots[_publishedSlot.load(std::memory_order_acquire)];
		_attFactor = published.attFactor;
		for (unsigned i = 0; i < _channelCount; i++)
		{
			_lowShelfBiquads[i].setCoefficients(published.aLS, published.a0LS);
			_highShelfBiquads[i].setCoefficients(published.aHS, published.a0HS);
		}
		_neutral = published.neutral;
		_coefficientConsumedEvent.set();
	}
	for (unsigned i = 0; i < _channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		for (unsigned j = 0; j < frameCount; j++)
		{
			_tempResult = _lowShelfBiquads[i].process(inputChannel[j]);
			_lowShelfBiquads[i].removeDenormals();
			_tempResult *= _attFactor;
			outputChannel[j] = _highShelfBiquads[i].process(_tempResult);
			_highShelfBiquads[i].removeDenormals();

			// if there is nearly no loudness correction necessary => set output=input to achive best quality
			if (_neutral)
			{
				outputChannel[j] = inputChannel[j];
			}
		}
	}
}

// Runs on the parameter update thread and writes into the slot that is not
// published yet, never into the one process() may be reading.
void LoudnessCorrectionFilter::upDateBiquadCoefficients(CoefficientSet& target, const double& freq, const double& bandwidthOrQOrS, const double& dbGain, bool highshelf)
{
	double A;
	A = pow(10, dbGain / 40);

	double omega = 2 * std::numbers::pi_v<double> * freq / _sampleRate;
	double sn = sin(omega);
	double cs = cos(omega);
	double alpha;
	double beta = -1;

	alpha = sn / 2 * sqrt((A + 1 / A) * (1 / bandwidthOrQOrS - 1) + 2);
	beta = 2 * sqrt(A) * alpha;

	double a0;
	if (highshelf)
	{
		a0 = (A + 1) - (A - 1) * cs + beta;
		target.a0HS = (A * ((A + 1) + (A - 1) * cs + beta)) / a0;
		target.aHS[0] = (-2 * A * ((A - 1) + (A + 1) * cs)) / a0;
		target.aHS[1] = (A * ((A + 1) + (A - 1) * cs - beta)) / a0;

		target.aHS[2] = (2 * ((A - 1) - (A + 1) * cs)) / a0;
		target.aHS[3] = ((A + 1) - (A - 1) * cs - beta) / a0;
	}
	else
	{
		a0 = (A + 1) + (A - 1) * cs + beta;
		target.a0LS = (A * ((A + 1) - (A - 1) * cs + beta)) / a0;
		target.aLS[0] = (2 * A * ((A - 1) - (A + 1) * cs)) / a0;
		target.aLS[1] = (A * ((A + 1) - (A - 1) * cs - beta)) / a0;

		target.aLS[2] = (-2 * ((A - 1) + (A + 1) * cs)) / a0;
		target.aLS[3] = ((A + 1) + (A - 1) * cs - beta) / a0;
	}
}
#pragma AVRT_CODE_END
