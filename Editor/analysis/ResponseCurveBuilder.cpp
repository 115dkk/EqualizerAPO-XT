/*
	This file is part of EqualizerAPO-XT.
*/

#include "ResponseCurveBuilder.h"

#include <algorithm>
#include <cmath>

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

// Linear interpolation of the metric between the two bins bracketing hz, on a
// logarithmic frequency axis.
//
// This reproduces GainIterator, which is what drew the magnitude curve before:
// same bracketing, same log-frequency parameter, same short circuit when both
// ends are equal (which is what let it carry -inf through). Beyond the last bin
// it holds the last value, and below the first it holds the first. The graph's
// upper frequency limit is capped at Nyquist, so the "hold the last value"
// branch no longer stretches a flat line across a decade of empty axis the way
// it did at sample rates below 40 kHz.
double interpolateAt(const AnalysisResponse& response, const QVector<double>& binValues, double hz)
{
	const size_t lastBin = static_cast<size_t>(binValues.size()) - 1;
	const double exact = hz * static_cast<double>(response.fftSize) / response.sampleRate;
	if (!std::isfinite(exact))
		return binValues[0];

	if (exact <= 0.0)
		return binValues[0];
	if (exact >= static_cast<double>(lastBin))
		return binValues[static_cast<int>(lastBin)];

	const size_t left = static_cast<size_t>(std::floor(exact));
	const size_t right = left + 1;
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
}

bool AnalysisCurve::isEmpty() const
{
	return values.isEmpty();
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
		// No values, but still a full axis: the same +/-12 dB grid, ticks and
		// captions the graph has always shown before its first analysis lands.
		// Returning a bare range instead would empty the horizontal grid and
		// every value figure out of the resting graph in all five skins.
		fitMagnitude(curve);
		return curve;
	}

	// Per-bin values first, then one interpolation pass per pixel column. The
	// per-bin pass is what makes the phase and group-delay metrics possible at
	// all: both are defined across neighbouring bins, not at a single one, so
	// they cannot be computed from a pixel's frequency alone.
	QVector<double> binValues(static_cast<int>(response.binCount()));
	switch (request.metric)
	{
	case AnalysisMetric::MagnitudeDb:
		for (int i = 0; i < binValues.size(); i++)
			binValues[i] = magnitudeDbAt(response, static_cast<size_t>(i));
		break;
	case AnalysisMetric::PhaseDegrees:
	case AnalysisMetric::GroupDelayMs:
		// Not yet: the phase pass lands with the views that show it. Until then
		// the request cannot be made from the UI.
		fitMagnitude(curve);
		return curve;
	}

	curve.values.resize(request.columnCount);
	for (int i = 0; i < request.columnCount; i++)
	{
		const double value = interpolateAt(response, binValues, analysisColumnFrequency(request, i));
		// A magnitude of exactly zero comes out as -inf and interpolating it
		// against a finite neighbour comes out NaN. The old path substituted a
		// display floor at that point, and a floor is right here: the value is
		// known, it is just too small to plot.
		curve.values[i] = std::isfinite(value) ? value : MagnitudeFloorDb;
	}

	fitMagnitude(curve);
	return curve;
}
