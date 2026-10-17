/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AnalysisPanelState.h"

#include <QCoreApplication>

// The texts keep MainWindow's translation context, where the panel lives.
AnalysisPanelState AnalysisPanelState::describe(const AnalysisPanelFacts& facts)
{
	AnalysisPanelState state;
	const QString unavailable = QString::fromUtf8("\xE2\x80\x94");

	// A line whose filter could not be set up rolled the whole load back: the
	// audio service keeps playing the previous configuration, and the
	// response the analysis still published was computed without this line.
	// No curve for a configuration that does not play. The notice stays
	// until a result without a setup error arrives; the exception text stays
	// in the log, which the engine already writes.
	if (facts.setupFailed)
	{
		state.notice = QCoreApplication::translate("MainWindow", "This configuration was not applied: the filter on line %1 of %2 could not be prepared. Equalizer APO keeps playing the previous settings.")
			.arg(facts.setupErrorLine)
			.arg(facts.setupErrorFileName);
		state.clearGraph = true;
		state.peakText = QCoreApplication::translate("MainWindow", "Not applied");
		state.peakToolTip = state.notice;
		state.peakSeverity = Severity::Critical;
		state.latencyText = unavailable;
		state.initializationText = unavailable;
		state.cpuText = unavailable;
		return state;
	}
	if (!facts.errorText.isEmpty())
	{
		state.peakText = QCoreApplication::translate("MainWindow", "Analysis failed");
		state.peakToolTip = facts.errorText;
		state.peakSeverity = Severity::Critical;
		state.latencyText = unavailable;
		state.initializationText = unavailable;
		state.cpuText = unavailable;
		return state;
	}

	state.peakText = QCoreApplication::translate("MainWindow", "%0 dB").arg(facts.peakGain, 0, 'f', 1);
	state.peakSeverity = facts.peakGain > 0 ? Severity::Critical : Severity::Normal;
	state.initializationText = QCoreApplication::translate("MainWindow", "%0 ms").arg(facts.initializationTime, 0, 'f', 1);

	if (facts.sampleRate <= 0 || facts.processedFrames <= 0)
	{
		state.latencyText = unavailable;
		state.cpuText = unavailable;
		return state;
	}

	// Milliseconds only. The frame count moved to the tooltip when the metric
	// switch and the base-delay option joined this bar: two readouts now share
	// a row, and "0.0 ms (0 s.)" does not fit beside another one inside the
	// bar's analysisControlBarWidth cap.
	const double latencyMs = facts.latencyFrames * 1000.0 / facts.sampleRate;
	state.latencyText = QCoreApplication::translate("MainWindow", "%0 ms").arg(latencyMs, 0, 'f', 1);
	state.latencyToolTip = QCoreApplication::translate("MainWindow", "%0 ms (%1 samples) of latency the analyzer removed before measuring")
		.arg(latencyMs, 0, 'f', 1).arg(facts.latencyFrames);

	const double cpuUsage = facts.processingTime * 100.0 / (facts.processedFrames * 1000.0 / facts.sampleRate);
	state.cpuText = QCoreApplication::translate("MainWindow", "%0 %").arg(cpuUsage, 0, 'f', 1);
	state.cpuSeverity = cpuUsage >= 50 ? Severity::Critical : (cpuUsage >= 20 ? Severity::Warning : Severity::Normal);
	return state;
}

const char* AnalysisPanelState::severityName(Severity severity)
{
	switch (severity)
	{
	case Severity::Warning:
		return "warning";
	case Severity::Critical:
		return "critical";
	case Severity::Normal:
		break;
	}
	return "normal";
}
