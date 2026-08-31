/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/


#include "ResponseCurveBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

#include <QtGlobal>

namespace
{
// Bin 0 sits at 0 Hz, which has no place on a logarithmic axis. The previous
// magnitude path moved it to 0.001 Hz for exactly that reason, and the
// interpolation below has to agree with it bin for bin, so the same nudge is
// applied here rather than a tidier one.
constexpr double DcNodeHz = 0.001;

// The graph's frequency window, unchanged since the analysis dock existed. The
// upper end is capped at Nyquist by analysisUpperFrequency.
constexpr double GraphMinHz = 20.0;
constexpr double GraphMaxHz = 20000.0;

// What the old path substituted for a magnitude that came out non-finite,
// which happens wherever the response is numerically zero. It is a display
// floor, not a change to the response.
constexpr double MagnitudeFloorDb = -120.0;

double binFrequency(const AnalysisResponse& response, size_t index)
{
	const double hz = response.frequencyOf(index);
	return hz == 0.0 ? DcNodeHz : hz;
}

double magnitudeDbAt(const AnalysisResponse& response, size_t index)
{
	// sqrt(re^2 + im^2) rather than std::abs, matching AnalysisPlotScene: the
	// two spellings can disagree in the last bit, and this path has to
	// reproduce the old magnitude curve exactly.
	const double re = response.bins[index].real();
	const double im = response.bins[index].imag();
	return std::log10(std::sqrt(re * re + im * im)) * 20.0;
}

// Below this fraction of the response's own peak, a bin carries no phase worth
// reading: what is left is the transform's own round-off, and its argument
// wanders at random. A cancellation that deep is what "Copy: L=L-L" or a notch
// landing exactly on a bin produces, and it is where an unguarded group delay
// explodes. Double-precision round-off across a 65536-point transform sits
// around 1e-15 of the peak, so this leaves four decades of headroom above the
// noise while still cutting a genuine null.
constexpr double PhaseFloorRatio = 1e-9;

// Per-bin values plus which of them mean anything. A false entry is a hole,
// not a zero: the curve breaks there rather than being drawn through it.
struct BinSeries
{
	QVector<double> values;
	QVector<bool> valid;
};

// Unwrapped phase in radians, with the analyzer's stripped bulk delay put back
// when asked for.
//
// The analyzer cuts the leading silence off before transforming, so the bins
// already describe the config with its bulk delay removed - which is what makes
// a filter's own phase readable at all, instead of buried under a ramp
// thousands of turns deep. Putting it back is the same linear phase in reverse:
// phase_full(f) = phase_aligned(f) - 2*pi*f*latency.
//
// Group delay is differentiated from this same series rather than computed
// separately, so the two views cannot disagree about the delay: subtracting a
// linear phase adds exactly the latency to the derivative, which is the
// relation the two modes are supposed to have.
BinSeries unwrappedPhase(const AnalysisResponse& response, bool includeLatency)
{
	const int binCount = static_cast<int>(response.binCount());
	BinSeries series;
	series.values.assign(binCount, 0.0);
	series.valid.assign(binCount, false);

	double peak = 0.0;
	for (const std::complex<double>& bin : response.bins)
		peak = std::max(peak, std::abs(bin));
	if (peak <= 0.0)
		return series;
	const double floor = peak * PhaseFloorRatio;

	const double latency = response.latencySeconds();
	double previousRaw = 0.0;
	double offset = 0.0;
	bool continuing = false;
	for (int i = 0; i < binCount; i++)
	{
		if (std::abs(response.bins[i]) <= floor)
		{
			continuing = false;
			continue;
		}
		const double raw = std::arg(response.bins[i]);
		if (continuing)
		{
			const double step = raw - previousRaw;
			if (step > std::numbers::pi_v<double>)
				offset -= 2.0 * std::numbers::pi_v<double>;
			else if (step < -std::numbers::pi_v<double>)
				offset += 2.0 * std::numbers::pi_v<double>;
		}
		else
		{
			// A new run starts from its own principal value. How much phase
			// accumulated across the hole is not knowable, so carrying the old
			// offset over would be an invention.
			offset = 0.0;
		}
		double value = raw + offset;
		if (includeLatency)
			value -= 2.0 * std::numbers::pi_v<double> * response.frequencyOf(static_cast<size_t>(i)) * latency;
		series.values[i] = value;
		series.valid[i] = true;
		previousRaw = raw;
		continuing = true;
	}
	return series;
}

// Group delay in milliseconds, differentiated from the unwrapped phase.
//
//   groupDelay(f) = -1 / (2*pi) * dphase(f) / df
//
// Central difference inside, one-sided at the two ends. A bin is left out when
// any bin the difference touches has no phase, because a difference across a
// hole measures the hole. No smoothing: a digital filter's group delay really
// does spike where its phase turns, and flattening that would hide the thing
// the view exists to show.
BinSeries groupDelayMs(const AnalysisResponse& response, const BinSeries& phase)
{
	const int binCount = phase.values.size();
	BinSeries series;
	series.values.assign(binCount, 0.0);
	series.valid.assign(binCount, false);
	if (binCount < 2 || response.fftSize == 0)
		return series;

	const double binHz = response.sampleRate / static_cast<double>(response.fftSize);
	if (binHz <= 0.0)
		return series;

	for (int i = 0; i < binCount; i++)
	{
		const int left = std::max(0, i - 1);
		const int right = std::min(binCount - 1, i + 1);
		if (left == right)
			continue;
		bool usable = true;
		for (int k = left; k <= right; k++)
			usable = usable && phase.valid[k];
		if (!usable)
			continue;
		const double slope = (phase.values[right] - phase.values[left]) / ((right - left) * binHz);
		series.values[i] = -slope / (2.0 * std::numbers::pi_v<double>) * 1000.0;
		series.valid[i] = true;
	}
	return series;
}

// Linear interpolation of the metric between the two bins bracketing hz, on a
// logarithmic frequency axis.
//
// This reproduces GainCurveIterator, which is what drew the magnitude curve before:
// same bracketing, same log-frequency parameter, same short circuit when both
// ends are equal (which is what let it carry -inf through). Beyond the last bin
// it holds the last value, and below the first it holds the first. The graph's
// upper frequency limit is capped at Nyquist, so the "hold the last value"
// branch no longer stretches a flat line across a decade of empty axis the way
// it did at sample rates below 40 kHz.
// A column whose bracketing bins carry no value reads as not-a-number, which is
// how the segment builder knows to break the line there.
double interpolateAt(const AnalysisResponse& response, const BinSeries& series, double hz)
{
	const double missing = std::numeric_limits<double>::quiet_NaN();
	const QVector<double>& binValues = series.values;
	const size_t lastBin = static_cast<size_t>(binValues.size()) - 1;
	const double exact = hz * static_cast<double>(response.fftSize) / response.sampleRate;
	if (!std::isfinite(exact))
		return series.valid[0] ? binValues[0] : missing;

	if (exact <= 0.0)
		return series.valid[0] ? binValues[0] : missing;
	if (exact >= static_cast<double>(lastBin))
		return series.valid[static_cast<int>(lastBin)] ? binValues[static_cast<int>(lastBin)] : missing;

	const size_t left = static_cast<size_t>(std::floor(exact));
	const size_t right = left + 1;
	if (!series.valid[static_cast<int>(left)] || !series.valid[static_cast<int>(right)])
		return missing;
	const double valueLeft = binValues[static_cast<int>(left)];
	const double valueRight = binValues[static_cast<int>(right)];
	if (valueLeft == valueRight)
		return valueLeft;

	const double logLeft = std::log(binFrequency(response, left));
	const double logSpan = std::log(binFrequency(response, right)) - logLeft;
	if (logSpan == 0.0)
		return valueLeft;
	const double t = (std::log(hz) - logLeft) / logSpan;
	return valueLeft + t * (valueRight - valueLeft);
}

void fitMagnitude(AnalysisCurve& curve)
{
	double maxAbs = 0.0;
	for (double value : curve.values)
		maxAbs = std::max(maxAbs, std::abs(value));

	// Symmetric around 0 dB, snapped to a 6 dB step, never tighter than +/-12
	// and never wider than +/-60. Unchanged from the previous path.
	const double range = qBound(12.0, std::ceil(maxAbs / 6.0) * 6.0, 60.0);
	curve.minimum = -range;
	curve.maximum = range;

	for (int db = static_cast<int>(curve.minimum); db <= static_cast<int>(curve.maximum); db += 6)
	{
		AnalysisCurveTick tick;
		tick.value = db;
		tick.label = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
		tick.major = db == 0;
		curve.ticks.append(tick);
	}

	curve.unit = QStringLiteral("dB");
	curve.topLabel = QStringLiteral("+%1 dB").arg(curve.maximum, 0, 'f', 0);
	curve.bottomLabel = QStringLiteral("%1 dB").arg(curve.minimum, 0, 'f', 0);
	curve.spanText = QStringLiteral("+%1 / %2 dB").arg(curve.maximum, 0, 'f', 0).arg(curve.minimum, 0, 'f', 0);

	for (double value : curve.values)
	{
		if (value > 0.05)
		{
			curve.clipping = true;
			break;
		}
	}
}

// Smallest step from the ladder that keeps the tick count near six. A ladder
// rather than a formula because the meaningful steps differ by metric: 90
// degrees is a landmark and 100 degrees is not, while a millisecond axis wants
// the ordinary 1/2/5 decades.
double chooseStep(double span, const double* ladder, int ladderSize)
{
	constexpr int targetTicks = 6;
	for (int i = 0; i < ladderSize; i++)
	{
		if (span / ladder[i] <= targetTicks)
			return ladder[i];
	}
	return ladder[ladderSize - 1];
}

QString signedNumber(double value, int decimals)
{
	const QString text = QString::number(value, 'f', decimals);
	return value > 0.0 ? QStringLiteral("+") + text : text;
}

// Fits the axis to the values that exist, snapped outward to whole steps, and
// writes the ticks and captions. Columns with no value are skipped: a hole must
// not drag the axis to cover a reading that was never taken.
void fitLinear(AnalysisCurve& curve, const QString& unit, int decimals,
	const double* ladder, int ladderSize, double minimumSpan, bool anchorAtZero)
{
	double lowest = std::numeric_limits<double>::infinity();
	double highest = -std::numeric_limits<double>::infinity();
	for (double value : curve.values)
	{
		if (!std::isfinite(value))
			continue;
		lowest = std::min(lowest, value);
		highest = std::max(highest, value);
	}
	if (!std::isfinite(lowest) || !std::isfinite(highest))
	{
		lowest = 0.0;
		highest = 0.0;
	}
	if (anchorAtZero)
	{
		// A group delay is a duration measured from no delay at all, so the
		// axis keeps zero in view even when every reading sits above it.
		lowest = std::min(lowest, 0.0);
		highest = std::max(highest, 0.0);
	}
	if (highest - lowest < minimumSpan)
	{
		const double centre = (highest + lowest) / 2.0;
		lowest = centre - minimumSpan / 2.0;
		highest = centre + minimumSpan / 2.0;
	}

	const double step = chooseStep(highest - lowest, ladder, ladderSize);
	curve.minimum = std::floor(lowest / step) * step;
	curve.maximum = std::ceil(highest / step) * step;
	if (curve.maximum <= curve.minimum)
		curve.maximum = curve.minimum + step;

	// Counted rather than accumulated, so a fractional step cannot drift the
	// last tick off the axis end.
	const int tickCount = static_cast<int>(std::lround((curve.maximum - curve.minimum) / step));
	for (int i = 0; i <= tickCount; i++)
	{
		const double value = curve.minimum + i * step;
		AnalysisCurveTick tick;
		tick.value = value;
		tick.label = signedNumber(value, decimals);
		tick.major = std::abs(value) < step * 1e-9;
		curve.ticks.append(tick);
	}

	curve.unit = unit;
	curve.topLabel = curve.formatValue(curve.maximum);
	curve.bottomLabel = curve.formatValue(curve.minimum);
	curve.spanText = QStringLiteral("%1 / %2").arg(signedNumber(curve.maximum, decimals),
		curve.formatValue(curve.minimum));
}

// Degrees: the landmark angles, not the 1/2/5 decades. A quarter turn has to
// land on a tick or the reading loses its meaning.
const double PhaseLadder[] = {15.0, 30.0, 45.0, 90.0, 180.0, 360.0, 720.0, 1440.0, 2880.0, 7200.0, 14400.0, 36000.0, 90000.0};
// Milliseconds: ordinary decades.
const double TimeLadder[] = {0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0};

// Runs the metric's own axis fit. Also the answer for a curve with no values at
// all, which is how the graph looks before its first analysis: an empty pane
// still needs a grid and captions in the unit the user asked to see.
void applyFit(AnalysisCurve& curve)
{
	switch (curve.metric)
	{
	case AnalysisMetric::MagnitudeDb:
		fitMagnitude(curve);
		return;
	case AnalysisMetric::PhaseDegrees:
		fitLinear(curve, QStringLiteral("\xC2\xB0"), 0,
			PhaseLadder, int(sizeof(PhaseLadder) / sizeof(PhaseLadder[0])), 180.0, false);
		return;
	case AnalysisMetric::GroupDelayMs:
		fitLinear(curve, QStringLiteral("ms"), 2,
			TimeLadder, int(sizeof(TimeLadder) / sizeof(TimeLadder[0])), 0.2, true);
		return;
	}
}
}

bool AnalysisCurve::isEmpty() const
{
	return values.isEmpty();
}

QString AnalysisCurve::formatValue(double value) const
{
	switch (metric)
	{
	case AnalysisMetric::PhaseDegrees:
		// No space: a degree sign sits against its number, unlike a word.
		return signedNumber(value, 1) + unit;
	case AnalysisMetric::GroupDelayMs:
		return QStringLiteral("%1 %2").arg(QString::number(value, 'f', 2), unit);
	case AnalysisMetric::MagnitudeDb:
		break;
	}
	return QStringLiteral("%1 %2").arg(QString::number(value, 'f', 1), unit);
}

double analysisColumnFrequency(const AnalysisCurveRequest& request, int column)
{
	if (request.columnCount <= 1)
		return request.minHz;
	const double t = static_cast<double>(column) / (request.columnCount - 1);
	return request.minHz * std::pow(request.maxHz / request.minHz, t);
}

double analysisUpperFrequency(const AnalysisResponse& response)
{
	const double nyquist = response.nyquist();
	if (nyquist <= GraphMinHz)
		return GraphMaxHz;
	return std::min(GraphMaxHz, nyquist);
}

QString analysisFrequencyCaption(double hz)
{
	if (hz >= 1000.0)
		return QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'g', 3);
	return QStringLiteral("%1 Hz").arg(hz, 0, 'g', 3);
}

double analysisValueToY(const QRectF& plotRect, double value, double minimum, double maximum)
{
	if (maximum <= minimum)
		return plotRect.center().y();
	const double bounded = qBound(minimum, value, maximum);
	const double t = (maximum - bounded) / (maximum - minimum);
	return plotRect.top() + plotRect.height() * t;
}

QVector<QPolygonF> buildCurveSegments(const QVector<double>& values, const QRectF& plotRect,
	double minimum, double maximum)
{
	QVector<QPolygonF> segments;
	QPolygonF current;
	for (int i = 0; i < values.size(); i++)
	{
		if (!std::isfinite(values[i]))
		{
			if (!current.isEmpty())
			{
				segments.append(current);
				current.clear();
			}
			continue;
		}
		current.append(QPointF(plotRect.left() + i,
			analysisValueToY(plotRect, values[i], minimum, maximum)));
	}
	if (!current.isEmpty())
		segments.append(current);
	return segments;
}

AnalysisCurve buildAnalysisCurve(const AnalysisResponse& response, const AnalysisCurveRequest& request)
{
	AnalysisCurve curve;
	curve.metric = request.metric;
	if (response.isEmpty() || request.columnCount <= 0 || request.maxHz <= request.minHz)
	{
		// No values, but still a full axis: for magnitude, the same +/-12 dB
		// grid, ticks and captions the graph has always shown before its first
		// analysis lands. Returning a bare range instead would empty the
		// horizontal grid and every value figure out of the resting graph in
		// all five skins.
		applyFit(curve);
		return curve;
	}

	// Per-bin values first, then one interpolation pass per pixel column. The
	// per-bin pass is what makes phase and group delay possible at all: both
	// are defined across neighbouring bins, not at a single one, so neither can
	// be computed from a pixel's frequency alone.
	const int binCount = static_cast<int>(response.binCount());
	BinSeries series;
	if (request.metric == AnalysisMetric::MagnitudeDb)
	{
		series.values.resize(binCount);
		series.valid.assign(binCount, true);
		for (int i = 0; i < binCount; i++)
			series.values[i] = magnitudeDbAt(response, static_cast<size_t>(i));
	}
	else
	{
		const BinSeries phase = unwrappedPhase(response, request.includeLatency);
		if (request.metric == AnalysisMetric::PhaseDegrees)
		{
			series = phase;
			for (int i = 0; i < binCount; i++)
				series.values[i] *= 180.0 / std::numbers::pi_v<double>;
		}
		else
		{
			series = groupDelayMs(response, phase);
		}
	}

	curve.values.resize(request.columnCount);
	for (int i = 0; i < request.columnCount; i++)
	{
		const double value = interpolateAt(response, series, analysisColumnFrequency(request, i));
		if (request.metric == AnalysisMetric::MagnitudeDb)
		{
			// A magnitude of exactly zero comes out as -inf, and interpolating
			// it against a finite neighbour comes out NaN. The old path
			// substituted a display floor there, and a floor is right: the
			// value is known, it is only too small to plot. Phase and group
			// delay have no such floor - where they are missing they are
			// genuinely missing, and the line breaks.
			curve.values[i] = std::isfinite(value) ? value : MagnitudeFloorDb;
		}
		else
		{
			curve.values[i] = value;
		}
	}

	applyFit(curve);
	return curve;
}
