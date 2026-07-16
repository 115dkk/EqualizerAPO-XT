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

#pragma once

#include <vector>

#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <fftw3.h>

#include "ConfigLoadTrace.h"
#include "DeviceAPOInfo.h"
#include "helpers/FftwRAII.h"

class AnalysisThread : public QThread
{
	Q_OBJECT

public:
	class ResultLock
	{
	public:
		ResultLock(const ResultLock&) = delete;
		ResultLock& operator=(const ResultLock&) = delete;

		fftw_complex* freqData() const;
		int freqDataLength() const;
		int freqDataSampleRate() const;
		double peakGain() const;
		int latency() const;
		double initializationTime() const;
		double processingTime() const;
		unsigned processedFrames() const;
		const std::vector<ConfigLoadTraceEntry>& loadTrace() const;

	private:
		friend class AnalysisThread;
		explicit ResultLock(AnalysisThread& owner);

		AnalysisThread& owner;
		QMutexLocker<QMutex> locker;
	};

	AnalysisThread();
	~AnalysisThread();
	void setParameters(std::shared_ptr<AbstractAPOInfo> device, int channelMask, int channelIndex, const QString& configPath, int frameCount);
	ResultLock lockResult();

signals:
	void analysisFinished();

protected:
	void run() override;

private:
	QMutex mutex;
	QWaitCondition condition;
	bool quit = false;

	// input
	std::shared_ptr<AbstractAPOInfo> device;
	int channelMask = 0;
	int channelIndex = 0;
	QString configPath;
	int frameCount = 0;

	// output
	fftw::ComplexBuffer resultFreqData;
	int freqDataLength = 0;
	int freqDataSampleRate = 0;
	double peakGain = 0.0;
	int latency = 0;
	double initializationTime = 0.0;
	double processingTime = 0.0;
	int processedFrames = 0;
	std::vector<ConfigLoadTraceEntry> resultLoadTrace;

	// internal (not protected by mutex)
	int lastFrameCount = -1;
	int lastChannelCount = -1;
	std::vector<double> buf;
	std::vector<double> buf2;
	fftw::RealBuffer timeData;
	fftw::ComplexBuffer freqData;
	fftw::Plan planForward;
};
