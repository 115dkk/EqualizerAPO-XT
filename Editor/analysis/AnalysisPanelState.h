/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What the analysis panel shows for one analysis result, as a decision
	table (audit #422 D4): a configuration that was not applied (a line's
	filter could not be set up, so the load rolled back), an analysis that
	failed, a result without a measurable run, and a measured one.
	MainWindow::updateAnalysisPanel gathers the facts and writes the
	readouts; the table has no widgets, so EditorLogicTests covers every row.
*/

#pragma once

#include <optional>

#include <QString>

struct AnalysisPanelFacts
{
	// The first line whose filter could not be set up: its 1-based line
	// number and the name of the file it is in.
	bool setupFailed = false;
	int setupErrorLine = 0;
	QString setupErrorFileName;
	// Why the analysis itself failed; empty when it ran.
	QString errorText;
	double peakGain = 0.0;
	int processedFrames = 0;
	int sampleRate = 0;
	int latencyFrames = 0;
	// Milliseconds.
	double initializationTime = 0.0;
	double processingTime = 0.0;
};

struct AnalysisPanelState
{
	enum class Severity
	{
		Normal,
		Warning,
		Critical
	};

	// The window-level notice; empty hides it.
	QString notice;
	// The graph shows no curve: the configuration does not play.
	bool clearGraph = false;
	QString peakText;
	QString peakToolTip;
	Severity peakSeverity = Severity::Normal;
	QString latencyText;
	// The latency readout's tooltip; left as it is when not set.
	std::optional<QString> latencyToolTip;
	QString initializationText;
	QString cpuText;
	Severity cpuSeverity = Severity::Normal;

	static AnalysisPanelState describe(const AnalysisPanelFacts& facts);
	// The value of the readouts' "severity" property the skins style.
	static const char* severityName(Severity severity);
};
