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

#include <chrono>

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
	{
		std::lock_guard<std::mutex> lock(_parameterUpdateThreadMutex);
		_stopParameterUpdateThread = true;
	}
	_parameterUpdateThreadCv.notify_all();
	if (_parameterUpdateThread.joinable())
		_parameterUpdateThread.join();
}

std::vector<std::wstring> LoudnessCorrectionFilter::initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	this->_channelCount = channelNames.size();
	_lowShelfBiquads.resize(_channelCount);
	_highShelfBiquads.resize(_channelCount);
	_sampleRate = sampleRate;
	_attFactor = 1.0;
	_neutral = true;

	double freqLS = 75, qLS = 1, gainLS = 0;
	double freqHS = 10000, qHS = 1, gainHS = 0;
	VolumeController VolumeController;
	double vol;
	HRESULT res = VolumeController.getVolume(vol);
	if (res == S_OK)
	{
		double preAmp;
		getLShelfParamter(vol, freqLS, qLS, gainLS, preAmp);
		_attFactor = exp(preAmp / 6 * log(2));
		getHShelfParamter(vol + preAmp, freqHS, qHS, gainHS);
		_neutral = std::max<double>(std::abs(gainLS), std::abs(gainHS)) < 0.2 ? true : false;
	}

	for (unsigned i = 0; i < _channelCount; i++)
	{
		_lowShelfBiquads[i] = BiQuad(BiQuad::LOW_SHELF, gainLS, freqLS, _sampleRate, qLS, false);
		_highShelfBiquads[i] = BiQuad(BiQuad::HIGH_SHELF, gainHS, freqHS, _sampleRate, qHS, false);
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

	{
		std::lock_guard<std::mutex> lock(_parameterUpdateThreadMutex);
		_stopParameterUpdateThread = false;
	}
	_parameterChanged.store(false);
	_parameterUpdateThread = std::thread(parameterUpdateThread, this);

	return channelNames;
}

void LoudnessCorrectionFilter::getLShelfParamter(const double& volume, double& frequence, double& q, double& gain, double& preAmp)
{
	frequence = 75;
	q = 0.52;
	double volDiff = _parameters.referenceLevel - _parameters.referenceOffset - volume;
	if (volDiff > 0)
	{
		// old: gain=volDiff*0.55*_parameters.attenuation;
		gain = volDiff * 0.55 / (1 - 0.55) * _parameters.attenuation;
		preAmp = -gain;
	}
	else if (volDiff < 0)
	{
		preAmp = 0.0;
		gain = volDiff * 0.55 * exp(volDiff / 90.0) * _parameters.attenuation;
	}
	else
	{
		gain = 0;
	}
}
void LoudnessCorrectionFilter::getHShelfParamter(const double& volume, double& frequence, double& q, double& gain)
{
	frequence = 10000;
	q = 0.9;
	double volDiff = _parameters.referenceLevel - _parameters.referenceOffset - volume;
	if (volDiff > 0)
	{
		gain = volDiff * 0.225 * exp(-volDiff / 100.0) * _parameters.attenuation;
	}
	else if (volDiff < 0)
	{
		gain = volDiff * 0.175 * exp(volDiff / 80.0) * _parameters.attenuation;
	}
	else
	{
		gain = 0;
	}
}

void LoudnessCorrectionFilter::parameterUpdateThread(LoudnessCorrectionFilter* lCorrection)
{
	VolumeController volumeController;
	double volOld(lCorrection->_parameters.referenceLevel);
	double vol(lCorrection->_parameters.referenceLevel);
	double freqLS, qLS, gainLS, preAmp;
	double freqHS, qHS, gainHS;
	HRESULT res;
	while (true)
	{
		{
			std::unique_lock<std::mutex> lock(lCorrection->_parameterUpdateThreadMutex);
			if (lCorrection->_parameterUpdateThreadCv.wait_for(lock, std::chrono::milliseconds(10), [lCorrection] {return lCorrection->_stopParameterUpdateThread; }))
				break;
		}

		// Skip while a published set is still unconsumed. This is also what keeps
		// two slots sufficient: the next fill cannot start until process() has
		// cleared the flag for the previous one.
		if (!lCorrection->_parameterChanged.load())
		{
			res = volumeController.getVolume(vol);
			if (res == S_OK)
			{
				if (vol != volOld)
				{
					lCorrection->getLShelfParamter(vol, freqLS, qLS, gainLS, preAmp);
					lCorrection->getHShelfParamter(vol + preAmp, freqHS, qHS, gainHS);

					// Fill the slot process() is not reading. Only this thread
					// writes _publishedSlot, so the load can be relaxed.
					const unsigned nextSlot = 1u - lCorrection->_publishedSlot.load(std::memory_order_relaxed);
					CoefficientSet& target = lCorrection->_coefficientSlots[nextSlot];
					target.attFactor = exp(preAmp / 6 * log(2));
					lCorrection->upDateBiquadCoefficients(target, freqHS, qHS, gainHS, true);
					lCorrection->upDateBiquadCoefficients(target, freqLS, qLS, gainLS, false);
					target.neutral = std::max<double>(std::abs(gainLS), std::abs(gainHS)) < 0.2 ? true : false;
					// Release: makes the writes above visible to the acquire load
					// in process(). Everything after this point must not touch the
					// slot again.
					lCorrection->_publishedSlot.store(nextSlot, std::memory_order_release);

					volOld = vol;

					lCorrection->_parameterChanged.store(true);
				}
			}
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
		// Acquire: pairs with the release store in parameterUpdateThread() and
		// makes that slot's coefficients visible here. No lock, so this cannot
		// wait on the normal-priority thread that calls COM through
		// VolumeController.
		const CoefficientSet& published = _coefficientSlots[_publishedSlot.load(std::memory_order_acquire)];
		_attFactor = published.attFactor;
		for (unsigned i = 0; i < _channelCount; i++)
		{
			_lowShelfBiquads[i].setCoefficients(published.aLS, published.a0LS);
			_highShelfBiquads[i].setCoefficients(published.aHS, published.a0HS);
		}
		_neutral = published.neutral;
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
