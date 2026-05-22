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

#include "ParameterArchive.h"
#include <IFilter.h>
#include <filters/BiQuad.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

#pragma AVRT_VTABLES_BEGIN
class LoudnessCorrectionFilter : public IFilter
{
public:
	struct FilterParameters
	{
		bool state;
		float referenceLevel;
		float referenceOffset;
		float attenuation;
		std::vector<char> serialize()
		{
			ParameterArchive archive; // possibility to get string from name of variable: std::string name=NAME(on);
			archive.add(state, L"State");
			archive.add(referenceLevel, L"ReferenceLevel");
			archive.add(referenceOffset, L"ReferenceOffset");
			archive.add(attenuation, L"Attenuation");
			return archive.getSerializedParameters();
		}
		template<typename T> bool deSerialize(const T& parameters)
		{
			ParameterArchive archive(parameters);
			int error(0);
			error += archive.get(state, std::wregex(L"\\s*State\\s+(0|1)"));
			error += archive.get(referenceLevel, std::wregex(L"\\s*ReferenceLevel\\s+([-+0-9]+)"));
			error += archive.get(referenceOffset, std::wregex(L"\\s*ReferenceOffset\\s+([-+0-9]+)"));
			// attenuation is only an optional parameter
			int errorAtt = archive.get(attenuation, std::wregex(L"\\s*Attenuation\\s+((1((\\.|,)0+)?)|(0((\\.|,)[0-9]+)?))"));
			if (errorAtt > 0)
			{
				attenuation = 1.0;
			}
			return error == 0 ? false : true;
		}
		FilterParameters()
			: _isInitialized(false) {};
		template<typename T> FilterParameters(T input)
		{
			_isInitialized = !deSerialize<T>(input);
		}
		bool isInitialized()
		{
			return _isInitialized;
		}
private:
		bool _isInitialized;
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
