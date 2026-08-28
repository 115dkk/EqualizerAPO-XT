/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Turns an AnalysisResponse into everything the graph needs to draw one
	metric: a value per pixel column, the fitted range, the value-axis ticks
	and the finished label strings.
*/

#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

#include "Editor/analysis/AnalysisMetric.h"
#include "Editor/analysis/AnalysisResponse.h"

struct AnalysisCurveRequest
{
	AnalysisMetric metric = AnalysisMetric::MagnitudeDb;
	// Put the analyzer's stripped bulk delay back into the phase. Ignored for
	// magnitude, which a pure delay cannot change.
	bool includeLatency = false;
	// Number of pixel columns to sample, inclusive of both ends. The graph
	// spans plotRect.width() pixels, which is width + 1 columns.
	int columnCount = 0;
	double minHz = 20.0;
	double maxHz = 20000.0;
};

// One value-axis gridline, with its label already formatted for the metric.
struct AnalysisCurveTick
{
	double value = 0.0;
	QString label;
	bool major = false;
};

struct AnalysisCurve
{
	AnalysisMetric metric = AnalysisMetric::MagnitudeDb;
	// One entry per pixel column, left to right. A non-finite entry marks a
	// column where the metric has no value - phase inside a notch's null, for
	// instance. Those columns break the drawn line rather than being bridged
	// across or flattened to zero, because either would invent a reading the
	// filter does not have.
	QVector<double> values;
	double minimum = 0.0;
	double maximum = 0.0;
	QVector<AnalysisCurveTick> ticks;
	// The metric's unit, so a skin can compose its own typography (upper case,
	// abbreviations) without knowing which metric is on screen.
	QString unit;
	// Finished strings for the two ends of the value axis and for a combined
	// span readout.
	QString topLabel;
	QString bottomLabel;
	QString spanText;
	// Magnitude above 0 dB, which can clip. Only ever true for magnitude: a
	// positive phase or a long group delay is not a danger state.
	bool clipping = false;

	bool isEmpty() const;
	// One reading, in the metric's own spelling: "+3.2 dB", "-181.4°",
	// "0.52 ms". The unit is not always separated by a space, so callers must
	// not build this by concatenation.
	QString formatValue(double value) const;
};

AnalysisCurve buildAnalysisCurve(const AnalysisResponse& response, const AnalysisCurveRequest& request);

// Maps a metric value onto a y inside plotRect, clamped to the fitted range.
double analysisValueToY(const QRectF& plotRect, double value, double minimum, double maximum);

// Splits per-column values into drawable polylines, starting a new one at every
// column whose value is not finite. A run of one valid column between two
// invalid ones yields a single-point polygon, which strokes as nothing; that is
// correct, because one column is not a line.
QVector<QPolygonF> buildCurveSegments(const QVector<double>& values, const QRectF& plotRect,
	double minimum, double maximum);

// Frequency of a pixel column, on the graph's logarithmic axis.
double analysisColumnFrequency(const AnalysisCurveRequest& request, int column);

// Where the graph's frequency axis ends: 20 kHz, or Nyquist when the device runs
// slower than 40 kHz. Above Nyquist there is no response, and holding the last
// bin's value across that stretch drew a flat line the config never produced.
double analysisUpperFrequency(const AnalysisResponse& response);

// "20 Hz", "20 kHz" - the axis end captions, formatted the way the graph has
// always shown them.
QString analysisFrequencyCaption(double hz);
