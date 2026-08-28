/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ResponseCurveBuilder: magnitude equivalence with the path it replaces, the
	range fit, the axis captions, and the segmentation that lets a curve break
	where the metric has no value.

	The equivalence test is the important one. The analysis graph used to be
	drawn by converting the complex spectrum to a list of dB nodes and running
	GainCurveIterator over them; it is now sampled from the spectrum directly. Those
	are two different pieces of code that have to agree exactly, or the reform
	quietly changes every magnitude curve a user has ever looked at. So this
	test builds the old node list, runs the real GainCurveIterator over it, and
	demands the same doubles - not a tolerance.
*/

#include <cmath>
#include <complex>
#include <limits>
#include <memory>
#include <vector>

#include "Editor/analysis/AnalysisResponse.h"
#include "Editor/analysis/ResponseCurveBuilder.h"
#include "filters/graphicEq/GainCurveIterator.h"

#include "EditorLogicTestSupport.h"

namespace
{
// A spectrum with peaks, dips, an overshoot and a numerically dead band, so the
// comparison covers the interesting parts of the magnitude range rather than a
// flat line.
std::shared_ptr<AnalysisResponse> makeSyntheticResponse(unsigned sampleRate = 48000, size_t fftSize = 8192)
{
	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = sampleRate;
	response->fftSize = fftSize;
	const size_t binCount = AnalysisResponse::binCountFor(fftSize);
	response->bins.resize(binCount);
	for (size_t i = 0; i < binCount; i++)
	{
		const double hz = response->frequencyOf(i);
		double db = 3.0 * std::sin(std::log(std::max(1.0, hz)) * 2.0) + 1.5;
		// A band that is exactly zero, so the -inf / NaN handling is exercised
		// rather than assumed.
		if (hz > 4000.0 && hz < 4400.0)
			db = -std::numeric_limits<double>::infinity();
		const double magnitude = std::isfinite(db) ? std::pow(10.0, db / 20.0) : 0.0;
		// Phase varies so the magnitude cannot accidentally come out right from
		// a real-only spectrum.
		const double phase = hz * 0.001;
		response->bins[i] = std::complex<double>(magnitude * std::cos(phase), magnitude * std::sin(phase));
	}
	return response;
}

// The dB node list exactly as AnalysisPlotScene builds it: fftSize / 2 nodes,
// bin 0 moved off 0 Hz because a logarithmic axis has no place for it, and
// sqrt(re^2 + im^2) rather than std::abs.
std::vector<FilterNode> legacyNodes(const AnalysisResponse& response)
{
	std::vector<FilterNode> nodes;
	const size_t count = response.fftSize / 2;
	nodes.reserve(count);
	for (size_t i = 0; i < count && i < response.bins.size(); i++)
	{
		double freq = response.frequencyOf(i);
		if (freq == 0.0)
			freq = 0.001;
		const double re = response.bins[i].real();
		const double im = response.bins[i].imag();
		nodes.emplace_back(freq, std::log10(std::sqrt(re * re + im * im)) * 20.0);
	}
	return nodes;
}
}

void testResponseCurveMatchesTheLegacyMagnitudePath()
{
	auto response = makeSyntheticResponse();

	AnalysisCurveRequest request;
	request.metric = AnalysisMetric::MagnitudeDb;
	// 904 columns is the width the analysis dock's graph actually samples at its
	// default size (940 wide, 18px margins, both ends inclusive).
	request.columnCount = 905;
	request.minHz = 20.0;
	request.maxHz = 20000.0;

	const AnalysisCurve curve = buildAnalysisCurve(*response, request);
	requireEqual(static_cast<int>(curve.values.size()), request.columnCount,
		QStringLiteral("the curve carries one value per pixel column"));

	const std::vector<FilterNode> nodes = legacyNodes(*response);
	GainCurveIterator iterator(nodes);
	int mismatches = 0;
	double worstDelta = 0.0;
	for (int i = 0; i < request.columnCount; i++)
	{
		const double hz = analysisColumnFrequency(request, i);
		const double legacy = iterator.gainAt(hz);
		// The old path floored a non-finite reading for display; so does the new
		// one, and at the same place.
		const double expected = std::isfinite(legacy) ? legacy : -120.0;
		const double actual = curve.values[i];
		if (actual != expected)
		{
			mismatches++;
			worstDelta = std::max(worstDelta, std::abs(actual - expected));
		}
	}
	expectTrue(mismatches == 0,
		QStringLiteral("every magnitude column matches GainCurveIterator exactly (%1 differed, worst %2 dB)")
			.arg(mismatches).arg(worstDelta, 0, 'g', 3));

	// The dead band has to survive as the display floor, not as a hole: a
	// magnitude of zero is a known reading that is simply too small to plot.
	bool sawFloor = false;
	for (int i = 0; i < curve.values.size(); i++)
	{
		if (curve.values[i] == -120.0)
			sawFloor = true;
		expectTrue(std::isfinite(curve.values[i]),
			QStringLiteral("magnitude column %1 is finite").arg(i));
	}
	expectTrue(sawFloor, QStringLiteral("the numerically dead band reads as the display floor"));
	expectTrue(curve.metric == AnalysisMetric::MagnitudeDb, QStringLiteral("the curve reports its metric"));
}

void testResponseCurveFitsAndLabelsTheValueAxis()
{
	// A quiet response never fits tighter than +/-12 dB, so the axis does not
	// magnify noise into a mountain range.
	auto quiet = makeSyntheticResponse();
	for (auto& bin : quiet->bins)
		bin = std::complex<double>(1.0, 0.0);

	AnalysisCurveRequest request;
	request.columnCount = 200;
	AnalysisCurve curve = buildAnalysisCurve(*quiet, request);
	expectTrue(curve.minimum == -12.0 && curve.maximum == 12.0,
		QStringLiteral("a flat response still fits +/-12 dB"));
	expectFalse(curve.clipping, QStringLiteral("a 0 dB response does not clip"));
	expectEqual(curve.topLabel, QStringLiteral("+12 dB"), QStringLiteral("top caption"));
	expectEqual(curve.bottomLabel, QStringLiteral("-12 dB"), QStringLiteral("bottom caption"));
	expectEqual(curve.spanText, QStringLiteral("+12 / -12 dB"), QStringLiteral("span caption"));
	expectEqual(curve.unit, QStringLiteral("dB"), QStringLiteral("magnitude is in dB"));

	// Five ticks across a +/-12 dB axis at a 6 dB step: -12, -6, 0, +6, +12.
	requireEqual(static_cast<int>(curve.ticks.size()), 5,
		QStringLiteral("the value axis is stepped every 6 dB"));
	expectEqual(curve.ticks[0].label, QStringLiteral("-12"), QStringLiteral("the bottom tick"));
	expectEqual(curve.ticks[2].label, QStringLiteral("0"), QStringLiteral("the zero tick"));
	expectTrue(curve.ticks[2].major, QStringLiteral("zero is the major tick"));
	expectEqual(curve.ticks[4].label, QStringLiteral("+12"), QStringLiteral("the top tick carries a sign"));
	expectFalse(curve.ticks[0].major, QStringLiteral("the ends are minor ticks"));

	// Anything above 0 dB is a clip, and the range snaps up in 6 dB steps
	// rather than hugging the peak.
	auto loud = makeSyntheticResponse();
	for (auto& bin : loud->bins)
		bin = std::complex<double>(std::pow(10.0, 14.0 / 20.0), 0.0);
	curve = buildAnalysisCurve(*loud, request);
	expectTrue(curve.clipping, QStringLiteral("a response above 0 dB clips"));
	expectTrue(curve.minimum == -18.0 && curve.maximum == 18.0,
		QStringLiteral("a 14 dB peak snaps the range to +/-18 dB"));

	// And it never opens past +/-60 dB, however extreme the response.
	auto extreme = makeSyntheticResponse();
	for (auto& bin : extreme->bins)
		bin = std::complex<double>(std::pow(10.0, 200.0 / 20.0), 0.0);
	curve = buildAnalysisCurve(*extreme, request);
	expectTrue(curve.minimum == -60.0 && curve.maximum == 60.0,
		QStringLiteral("the range stops opening at +/-60 dB"));
}

void testResponseCurveHandlesEmptyAndDegenerateRequests()
{
	AnalysisResponse empty;
	AnalysisCurveRequest request;
	request.columnCount = 100;

	AnalysisCurve curve = buildAnalysisCurve(empty, request);
	expectTrue(curve.isEmpty(), QStringLiteral("no response means no curve"));
	// The axis still has to be usable, or the grid collapses onto one line
	// while the graph waits for its first analysis.
	expectTrue(curve.minimum == -12.0 && curve.maximum == 12.0,
		QStringLiteral("an empty curve keeps a drawable axis"));

	auto response = makeSyntheticResponse();
	request.columnCount = 0;
	expectTrue(buildAnalysisCurve(*response, request).isEmpty(),
		QStringLiteral("a zero-width graph asks for no columns"));
	request.columnCount = 100;
	request.maxHz = request.minHz;
	expectTrue(buildAnalysisCurve(*response, request).isEmpty(),
		QStringLiteral("a collapsed frequency window produces no curve"));
}

void testResponseCurveFrequencyAxis()
{
	AnalysisCurveRequest request;
	request.columnCount = 3;
	request.minHz = 20.0;
	request.maxHz = 20000.0;
	// Logarithmic: the middle column of three sits at the geometric mean.
	expectTrue(std::abs(analysisColumnFrequency(request, 0) - 20.0) < 1e-9,
		QStringLiteral("the first column is the lower limit"));
	expectTrue(std::abs(analysisColumnFrequency(request, 1) - std::sqrt(20.0 * 20000.0)) < 1e-6,
		QStringLiteral("the middle column is the geometric mean"));
	expectTrue(std::abs(analysisColumnFrequency(request, 2) - 20000.0) < 1e-6,
		QStringLiteral("the last column is the upper limit"));

	request.columnCount = 1;
	expectTrue(analysisColumnFrequency(request, 0) == 20.0,
		QStringLiteral("a single column degenerates to the lower limit rather than dividing by zero"));

	expectEqual(analysisFrequencyCaption(20.0), QStringLiteral("20 Hz"), QStringLiteral("the lower caption"));
	expectEqual(analysisFrequencyCaption(20000.0), QStringLiteral("20 kHz"), QStringLiteral("the upper caption"));
	expectEqual(analysisFrequencyCaption(16000.0), QStringLiteral("16 kHz"), QStringLiteral("a Nyquist-capped caption"));

	// The axis stops at Nyquist. Past it the analyzer has no bins, and the old
	// path held the last bin's value across the gap - a flat line the config
	// never produced, visible on any device running below 40 kHz.
	auto fast = makeSyntheticResponse(48000);
	expectTrue(analysisUpperFrequency(*fast) == 20000.0,
		QStringLiteral("at 48 kHz the axis ends at 20 kHz as it always has"));
	auto slow = makeSyntheticResponse(32000);
	expectTrue(analysisUpperFrequency(*slow) == 16000.0,
		QStringLiteral("at 32 kHz the axis ends at Nyquist"));
	AnalysisResponse none;
	expectTrue(analysisUpperFrequency(none) == 20000.0,
		QStringLiteral("with no response the axis keeps its full window"));
}

void testResponseCurveSegmentsBreakWhereTheValueIsMissing()
{
	const QRectF plot(10.0, 0.0, 100.0, 100.0);
	const double nan = std::numeric_limits<double>::quiet_NaN();

	// One run of valid columns is one polyline, and its x positions start at the
	// plot's left edge.
	QVector<QPolygonF> segments = buildCurveSegments({1.0, 2.0, 3.0}, plot, -10.0, 10.0);
	requireEqual(static_cast<int>(segments.size()), 1, QStringLiteral("contiguous values make one segment"));
	expectEqual(static_cast<int>(segments[0].size()), 3, QStringLiteral("every column becomes a point"));
	expectTrue(segments[0][0].x() == 10.0 && segments[0][2].x() == 12.0,
		QStringLiteral("columns are laid out from the plot's left edge"));

	// A hole splits the line rather than being bridged: joining across it would
	// claim the response passed through readings it never had.
	segments = buildCurveSegments({1.0, 2.0, nan, 4.0, 5.0}, plot, -10.0, 10.0);
	requireEqual(static_cast<int>(segments.size()), 2, QStringLiteral("a missing column splits the line"));
	expectEqual(static_cast<int>(segments[0].size()), 2, QStringLiteral("the first run keeps its two columns"));
	expectEqual(static_cast<int>(segments[1].size()), 2, QStringLiteral("the second run keeps its two columns"));
	expectTrue(segments[1][0].x() == 13.0,
		QStringLiteral("the second run resumes after the hole, not at its start"));

	// Leading and trailing holes produce no empty segments to draw.
	segments = buildCurveSegments({nan, nan, 3.0, nan}, plot, -10.0, 10.0);
	requireEqual(static_cast<int>(segments.size()), 1, QStringLiteral("holes at the ends do not create empty segments"));
	expectEqual(static_cast<int>(segments[0].size()), 1, QStringLiteral("a lone valid column is a one-point segment"));

	segments = buildCurveSegments({nan, nan}, plot, -10.0, 10.0);
	expectEqual(static_cast<int>(segments.size()), 0, QStringLiteral("nothing valid means nothing to draw"));
	segments = buildCurveSegments({}, plot, -10.0, 10.0);
	expectEqual(static_cast<int>(segments.size()), 0, QStringLiteral("no columns means nothing to draw"));

	// Infinities count as missing too, not as an off-scale reading pinned to the
	// edge.
	segments = buildCurveSegments({1.0, std::numeric_limits<double>::infinity(), 3.0}, plot, -10.0, 10.0);
	expectEqual(static_cast<int>(segments.size()), 2, QStringLiteral("an infinite column breaks the line"));

	// Values are clamped into the fitted range, and the mapping is inverted:
	// the maximum is at the top.
	expectTrue(analysisValueToY(plot, 10.0, -10.0, 10.0) == 0.0, QStringLiteral("the maximum maps to the top"));
	expectTrue(analysisValueToY(plot, -10.0, -10.0, 10.0) == 100.0, QStringLiteral("the minimum maps to the bottom"));
	expectTrue(analysisValueToY(plot, 0.0, -10.0, 10.0) == 50.0, QStringLiteral("zero maps to the middle"));
	expectTrue(analysisValueToY(plot, 999.0, -10.0, 10.0) == 0.0, QStringLiteral("an out-of-range value clamps"));
	expectTrue(analysisValueToY(plot, 5.0, 10.0, 10.0) == 50.0,
		QStringLiteral("a collapsed range answers the middle rather than dividing by zero"));
}
