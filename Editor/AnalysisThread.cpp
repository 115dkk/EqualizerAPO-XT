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

#include "audio/ChannelLayout.h"
#include "dsp/FftwPlanningPolicy.h"
#include "engine/FilterEngine.h"
#include "Editor/analysis/ImpulseMeasurement.h"
#include "helpers/AnalysisWorkerRecovery.h"
#include "services/logging/Logging.h"
#include "AnalysisThread.h"

using std::mutex;
using std::numeric_limits;
using std::shared_ptr;

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
	requestTicket = requestFence.begin();

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

std::shared_ptr<const AnalysisResponse> AnalysisThread::ResultLock::response() const
{
	return owner.resultResponse;
}

double AnalysisThread::ResultLock::peakGain() const
{
	return owner.peakGain;
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

const QString& AnalysisThread::ResultLock::errorText() const
{
	return owner.resultErrorText;
}

const std::vector<ConfigLoadTraceEntry>& AnalysisThread::ResultLock::loadTrace() const
{
	return owner.resultLoadTrace;
}

void AnalysisThread::run()
{
	while (true)
	{
		shared_ptr<AbstractAPOInfo> device;
		int channelMask;
		int channelIndex;
		QString configPath;
		int frameCount;
		AnalysisRequestFence::Ticket ticket;
		{
			QMutexLocker locker(&mutex);
			while (!quit.load(std::memory_order_relaxed) && this->frameCount == 0)
				condition.wait(&mutex);
			if (quit.load(std::memory_order_relaxed))
				break;

			device = this->device;
			channelMask = this->channelMask;
			channelIndex = this->channelIndex;
			configPath = this->configPath;
			frameCount = this->frameCount;
			ticket = requestTicket;
			this->frameCount = 0;
		}

		if (frameCount <= 0)
		{
			qWarning("Analysis skipped an invalid frame count: %d", frameCount);
			continue;
		}

		bool resultPublished = false;
		AnalysisWorkerRecovery::run([&]
		{
		QElapsedTimer timer;
		timer.start();

		const ChannelLayout::AnalysisLayout layout = ChannelLayout::analysisLayout(
			device->getChannelCount(), device->getChannelMask(), channelMask);
		const unsigned channelCount = layout.channelCount;
		channelMask = layout.channelMask;
		// channelIndex is a position in the analysis channel list, built from
		// the same layout; an index outside it would read past the buffer.
		if (channelIndex < 0 || static_cast<unsigned>(channelIndex) >= channelCount)
			throw std::out_of_range("the analysis channel is outside the stream's channel layout");

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
		engine.setAnalysisMode(true);
		engine.setLoadTraceSink(&traceCollector);
		EngineSetup setup;
		setup.sampleRate = sampleRate;
		setup.inputChannelCount = channelCount;
		setup.realChannelCount = channelCount;
		setup.outputChannelCount = channelCount;
		setup.channelMask = channelMask;
		setup.maxFrameCount = frameCount;
		setup.customPath = configPath.toStdWString();
		setup.capture = device->isInput();
		setup.deviceName = device->getDeviceName();
		setup.connectionName = device->getConnectionName();
		setup.deviceGuid = device->getDeviceGuid();
		engine.initialize(setup);
		engine.setLoadTraceSink(nullptr);
		if (quit.load(std::memory_order_relaxed))
			return;
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
			// A real-to-complex transform writes frameCount / 2 + 1 bins and
			// nothing beyond them. Allocating frameCount of them, as this used
			// to, wasted half the buffer and left the tail uninitialized for
			// anyone who read the whole thing.
			auto newFreqData = fftw::allocateComplex(AnalysisResponse::binCountFor(frameCount));
			fftw::Plan newPlan;
			{
				// Plan creation happens under the process-wide planning policy
				// (see dsp/FftwPlanningPolicy.h).
				FftwPlanningPolicy::Session planning;
				newPlan = fftw::makeRealToComplexPlan(frameCount, newTimeData.get(), newFreqData.get());
			}
			planForward.reset();
			timeData = std::move(newTimeData);
			freqData = std::move(newFreqData);
			planForward = std::move(newPlan);
		}

		lastFrameCount = frameCount;
		lastChannelCount = channelCount;

		ImpulseMeasurement measurement(
			channelCount, channelIndex, frameCount, timeData.get());
		double processingTime = 0.0;
		unsigned processedFrames = 0;
		// stop searching for the impulse after 10 seconds of audio data
		while (processedFrames < 10 * sampleRate)
		{
			if (quit.load(std::memory_order_relaxed))
				return;

			qint64 startTime = timer.nsecsElapsed();
			engine.process(buf2.data(), buf.data(), frameCount);
			processingTime += (timer.nsecsElapsed() - startTime) / 1e6;
			processedFrames += frameCount;

			if (measurement.addBlock(buf2.data()))
				break;

			// The impulse is one frame long: after the first block the engine
			// is fed silence.
			if (processedFrames == static_cast<unsigned>(frameCount))
			{
				for (unsigned i = 0; i < channelCount; i++)
					buf[i] = 0.0f;
			}
		}

		int latency;
		double peakGain;
		if (measurement.found())
		{
			latency = measurement.latencyFrames();

			fftw_execute(planForward.get());

			peakGain = impulsePeakGainDb(freqData.get(),
				AnalysisResponse::binCountFor(frameCount));
		}
		else
		{
			latency = 0;
			peakGain = -numeric_limits<double>::infinity();
			std::fill_n(&freqData.get()[0][0], AnalysisResponse::binCountFor(frameCount) * 2, 0.0);
		}

		// Built outside the lock: the copy out of the FFTW buffer is the only
		// unavoidable one, and the UI should not wait behind it. Publishing is
		// then a pointer swap, and what it points at is never touched again, so
		// a reader can let go of the mutex and still build a curve safely.
		auto response = std::make_shared<AnalysisResponse>();
		response->sampleRate = static_cast<unsigned>(sampleRate);
		response->fftSize = static_cast<size_t>(frameCount);
		response->latencyFrames = latency;
		response->frozenDynamicResponse = engine.usedFrozenDynamicAnalysis();
		const size_t binCount = AnalysisResponse::binCountFor(frameCount);
		response->bins.resize(binCount);
		for (size_t i = 0; i < binCount; i++)
			response->bins[i] = std::complex<double>(freqData.get()[i][0], freqData.get()[i][1]);

		{
			QMutexLocker locker(&mutex);
			if (!requestFence.publishIf(ticket, [&]
			{
				this->resultResponse = std::move(response);
				this->peakGain = peakGain;
				this->initializationTime = initializationTime;
				this->processingTime = processingTime;
				this->processedFrames = processedFrames;
				this->resultErrorText.clear();
				this->resultLoadTrace = std::move(traceCollector.entries);
				resultPublished = true;
			}))
				return;
		}

		TraceF(L"Analysis took %.1f ms", timer.nsecsElapsed() / 1e6);
		},
		[&](const char* error)
		{
			qCritical("Analysis failed; worker remains available: %s", error);
			QMutexLocker locker(&mutex);
			requestFence.publishIf(ticket, [&]
			{
				// An empty response rather than a null one, so the graph clears
				// instead of keeping the previous config's curve on screen.
				resultResponse = std::make_shared<AnalysisResponse>();
				peakGain = numeric_limits<double>::quiet_NaN();
				initializationTime = 0.0;
				processingTime = 0.0;
				processedFrames = 0;
				resultErrorText = QString::fromUtf8(error);
				resultLoadTrace.clear();
				resultPublished = true;
			});
		});

		bool emitFinished = false;
		if (resultPublished)
		{
			QMutexLocker locker(&mutex);
			requestFence.publishIf(ticket, [&]
			{
				emitFinished = true;
			});
		}
		if (emitFinished)
			emit analysisFinished();
	}
}
