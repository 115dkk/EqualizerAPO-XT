/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#include <QElapsedTimer>

#include <stdexcept>

#include "FilterEngine.h"
#include "AnalysisThread.h"

using std::abs;
using std::log10;
using std::mutex;
using std::numeric_limits;
using std::shared_ptr;
using std::sqrt;

AnalysisThread::AnalysisThread()
{
}

AnalysisThread::~AnalysisThread()
{
	{
		QMutexLocker locker(&mutex);
		quit = true;
		condition.wakeAll();
	}

	wait();
}

void AnalysisThread::setParameters(shared_ptr<AbstractAPOInfo> device, int channelMask, int channelIndex, const QString& configPath, int frameCount)
{
	QMutexLocker mutexLocker(&mutex);
	this->device = device;
	this->channelMask = channelMask;
	this->channelIndex = channelIndex;
	this->configPath = configPath;
	this->frameCount = frameCount;

	condition.wakeAll();
}

AnalysisThread::ResultLock AnalysisThread::lockResult()
{
	return ResultLock(*this);
}

AnalysisThread::ResultLock::ResultLock(AnalysisThread& owner)
	: owner(owner), locker(&owner.mutex)
{
}

fftw_complex* AnalysisThread::ResultLock::freqData() const
{
	return owner.resultFreqData.get();
}

int AnalysisThread::ResultLock::freqDataLength() const
{
	return owner.freqDataLength;
}

int AnalysisThread::ResultLock::freqDataSampleRate() const
{
	return owner.freqDataSampleRate;
}

double AnalysisThread::ResultLock::peakGain() const
{
	return owner.peakGain;
}

int AnalysisThread::ResultLock::latency() const
{
	return owner.latency;
}

double AnalysisThread::ResultLock::initializationTime() const
{
	return owner.initializationTime;
}

double AnalysisThread::ResultLock::processingTime() const
{
	return owner.processingTime;
}

unsigned AnalysisThread::ResultLock::processedFrames() const
{
	return owner.processedFrames;
}

const std::vector<ConfigLoadTraceEntry>& AnalysisThread::ResultLock::loadTrace() const
{
	return owner.resultLoadTrace;
}

void AnalysisThread::run()
try
{
	while (true)
	{
		shared_ptr<AbstractAPOInfo> device;
		int channelMask;
		int channelIndex;
		QString configPath;
		int frameCount;
		{
			QMutexLocker locker(&mutex);
			while (!quit && this->frameCount == 0)
				condition.wait(&mutex);
			if (quit)
				break;

			device = this->device;
			channelMask = this->channelMask;
			channelIndex = this->channelIndex;
			configPath = this->configPath;
			frameCount = this->frameCount;
			this->frameCount = 0;
		}

		if (frameCount <= 0)
		{
			qWarning("Analysis skipped an invalid frame count: %d", frameCount);
			continue;
		}

		QElapsedTimer timer;
		timer.start();

		unsigned channelCount = device->getChannelCount();
		if (channelMask != 0 && channelMask != device->getChannelMask())
		{
			channelCount = 0;
			for (int i = 0; i < 31; i++)
			{
				int channelPos = 1 << i;
				if (channelMask & channelPos)
					channelCount++;
			}
		}
		if (channelCount == 0)
		{
			channelCount = 8;
			channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		}

		unsigned sampleRate = device->getSampleRate();
		if (sampleRate == 0)
			sampleRate = 48000;

		qint64 startTime = timer.nsecsElapsed();

		// Collects the engine's per-line load facts (branch decisions, Eval
		// values, skipped lines). With a custom config path initialize() loads
		// synchronously and starts no notification worker, so a plain vector
		// needs no locking here.
		struct Collector : ConfigLoadTraceSink
		{
			std::vector<ConfigLoadTraceEntry> entries;
			void addEntry(const ConfigLoadTraceEntry& entry) override
			{
				entries.push_back(entry);
			}
		};
		Collector traceCollector;

		FilterEngine engine;
		engine.setLoadTraceSink(&traceCollector);
		engine.setDeviceInfo(device->isInput(), true, device->getDeviceName(), device->getConnectionName(), device->getDeviceGuid(), device->getDeviceString());
		engine.initialize(sampleRate, channelCount, channelCount, channelCount, channelMask, frameCount, configPath.toStdWString());
		engine.setLoadTraceSink(nullptr);
		double initializationTime = (timer.nsecsElapsed() - startTime) / 1e6;

		if (frameCount != lastFrameCount || channelCount != lastChannelCount)
		{
			if (channelCount != 0
				&& static_cast<size_t>(frameCount) > (numeric_limits<size_t>::max)() / channelCount)
			{
				throw std::length_error("Analysis buffer size overflow");
			}
			const size_t sampleCount = static_cast<size_t>(frameCount) * channelCount;
			std::vector<double> newBuf(sampleCount, 0.0);
			std::vector<double> newBuf2(sampleCount);
			buf = std::move(newBuf);
			buf2 = std::move(newBuf2);
		}
		for (unsigned i = 0; i < channelCount; i++)
			buf[i] = 1.0f;

		if (frameCount != lastFrameCount)
		{
			auto newTimeData = fftw::allocateReal(frameCount);
			auto newFreqData = fftw::allocateComplex(frameCount);
			auto newPlan = fftw::makeRealToComplexPlan(frameCount, newTimeData.get(), newFreqData.get());
			planForward.reset();
			timeData = std::move(newTimeData);
			freqData = std::move(newFreqData);
			planForward = std::move(newPlan);
		}

		lastFrameCount = frameCount;
		lastChannelCount = channelCount;

		int latency = 0;
		int startFrame = -1;
		double processingTime = 0.0;
		unsigned processedFrames = 0;
		// stop searching for startFrame after 10 seconds of audio data
		while (processedFrames < 10 * sampleRate)
		{
			qint64 startTime = timer.nsecsElapsed();
			engine.process(buf2.data(), buf.data(), frameCount);
			processingTime += (timer.nsecsElapsed() - startTime) / 1e6;
			processedFrames += frameCount;

			if (startFrame != -1)
			{
				for (int i = 0; i < startFrame; i++)
				{
					timeData.get()[frameCount - startFrame + i] = buf2[i * channelCount + channelIndex];
				}
				break;
			}

			for (int i = 0; i < frameCount; i++)
			{
				double s = buf2[i * channelCount + channelIndex];
				if (abs(s) > 1e-5f)
				{
					startFrame = i;
					break;
				}
			}

			if (startFrame != -1)
			{
				for (int i = 0; i < frameCount - startFrame; i++)
				{
						timeData.get()[i] = buf2[(startFrame + i) * channelCount + channelIndex];
				}

				if (startFrame == 0)
					break;
			}

			if (latency == 0)
			{
				for (unsigned i = 0; i < channelCount; i++)
					buf[i] = 0.0f;
			}

			if (startFrame == -1)
				latency += frameCount;
		}

		double peakGain;
		if (startFrame != -1)
		{
			latency += startFrame;

			fftw_execute(planForward.get());

			peakGain = -DBL_MAX;

			for (int i = 0; i < frameCount / 2; i++)
			{
				double sqrGain = freqData.get()[i][0] * freqData.get()[i][0] + freqData.get()[i][1] * freqData.get()[i][1];
				if (sqrGain > peakGain)
					peakGain = sqrGain;
			}
			peakGain = sqrt(peakGain);
			peakGain = log10(peakGain) * 20.0;
		}
		else
		{
			latency = 0;
			peakGain = -numeric_limits<double>::infinity();
			std::fill_n(&freqData.get()[0][0], frameCount * 2, 0.0);
		}

		{
			QMutexLocker locker(&mutex);
			if (this->freqDataLength != frameCount)
				resultFreqData = fftw::allocateComplex(frameCount);
			std::copy_n(&freqData.get()[0][0], frameCount * 2, &resultFreqData.get()[0][0]);
			this->freqDataLength = frameCount;
			this->freqDataSampleRate = sampleRate;
			this->latency = latency;
			this->peakGain = peakGain;
			this->initializationTime = initializationTime;
			this->processingTime = processingTime;
			this->processedFrames = processedFrames;
			this->resultLoadTrace = std::move(traceCollector.entries);
		}

		qDebug("Analysis took %.1f ms", timer.nsecsElapsed() / 1e6);

		emit analysisFinished();
	}
}
catch (const std::exception& error)
{
	qCritical("Analysis thread stopped after an exception: %s", error.what());
}
catch (...)
{
	qCritical("Analysis thread stopped after a non-standard exception");
}
