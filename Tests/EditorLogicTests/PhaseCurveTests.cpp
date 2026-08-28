/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Phase and group delay, checked against filters whose answers are known in
	closed form rather than against a previous run of the same code.

	The all-pass cases build their spectra from the engine's own BiQuad
	coefficients, so a disagreement between what the graph draws and what the
	filter does shows up here rather than in a user's ears. The pure-delay case
	pins the two things the "include base delay" switch has to get right: with
	it off a delay is invisible, which is the point of stripping it, and with it
	on the group delay is exactly the delay.
*/

// Before anything that reaches <math.h>.
#define _USE_MATH_DEFINES

#include <cmath>
#include <complex>
#include <limits>
#include <memory>

#include "Editor/analysis/AnalysisResponse.h"
#include "Editor/analysis/ResponseCurveBuilder.h"
#include "filters/BiQuad.h"

#include "EditorLogicTestSupport.h"

namespace
{
constexpr unsigned SampleRate = 48000;
constexpr size_t FftSize = 32768;

std::shared_ptr<AnalysisResponse> emptyResponse()
{
	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = SampleRate;
	response->fftSize = FftSize;
	response->bins.assign(AnalysisResponse::binCountFor(FftSize), std::complex<double>(1.0, 0.0));
	return response;
}

// The response of a biquad, evaluated on the unit circle at every bin. Built
// from the engine's coefficients, not from a textbook copy of them.
std::shared_ptr<AnalysisResponse> biquadResponse(BiQuad::Type type, double freq, double q)
{
	BiQuad biquad(type, 0.0, freq, SampleRate, q, false);
	double packed[4];
	double b0 = 0.0;
	biquad.getCoefficients(packed, b0);
	const double b1 = packed[0];
	const double b2 = packed[1];
	const double a1 = packed[2];
	const double a2 = packed[3];

	auto response = emptyResponse();
	for (size_t i = 0; i < response->bins.size(); i++)
	{
		const double omega = 2.0 * M_PI * response->frequencyOf(i) / SampleRate;
		const std::complex<double> z1 = std::polar(1.0, -omega);
		const std::complex<double> z2 = z1 * z1;
		response->bins[i] = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
	}
	return response;
}

AnalysisCurveRequest requestFor(AnalysisMetric metric, bool includeLatency = false)
{
	AnalysisCurveRequest request;
	request.metric = metric;
	request.includeLatency = includeLatency;
	request.columnCount = 905;
	request.minHz = 20.0;
	request.maxHz = 20000.0;
	return request;
}

// The graph samples per pixel, so no column sits exactly on a round frequency.
// Tests that compare against a closed form have to ask which frequency the
// column they picked actually stands for, or they measure the axis quantization
// instead of the filter.
int columnNearest(const AnalysisCurveRequest& request, double hz)
{
	int best = 0;
	double bestDistance = std::numeric_limits<double>::infinity();
	for (int i = 0; i < request.columnCount; i++)
	{
		const double distance = std::abs(analysisColumnFrequency(request, i) - hz);
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

double valueNearest(const AnalysisCurve& curve, const AnalysisCurveRequest& request, double hz)
{
	return curve.values[columnNearest(request, hz)];
}
}

void testPhaseAndGroupDelayOfAUnityResponse()
{
	auto response = emptyResponse();

	AnalysisCurveRequest request = requestFor(AnalysisMetric::PhaseDegrees);
	AnalysisCurve curve = buildAnalysisCurve(*response, request);
	double worst = 0.0;
	for (double value : curve.values)
		worst = std::max(worst, std::abs(value));
	expectTrue(worst < 1e-9, QStringLiteral("a unity response has no phase (worst %1 deg)").arg(worst));
	expectEqual(curve.unit, QString::fromUtf8("\xC2\xB0"), QStringLiteral("phase is in degrees"));

	request = requestFor(AnalysisMetric::GroupDelayMs);
	curve = buildAnalysisCurve(*response, request);
	worst = 0.0;
	for (double value : curve.values)
		worst = std::max(worst, std::abs(value));
	expectTrue(worst < 1e-9, QStringLiteral("a unity response has no group delay (worst %1 ms)").arg(worst));
	expectEqual(curve.unit, QStringLiteral("ms"), QStringLiteral("group delay is in milliseconds"));
	// The axis keeps zero in view even though every reading sits on it.
	expectTrue(curve.minimum <= 0.0 && curve.maximum >= 0.0,
		QStringLiteral("the group-delay axis contains zero"));
}

void testPhaseAndGroupDelayOfAPureDelay()
{
	// What the analyzer hands over for a config that is nothing but a delay:
	// it strips the silence, so the bins are unity, and reports the frames it
	// took off separately. Without that strip a filter's own phase would be
	// buried under a ramp thousands of turns deep.
	auto response = emptyResponse();
	response->latencyFrames = 480; // 10 ms at 48 kHz

	AnalysisCurveRequest request = requestFor(AnalysisMetric::GroupDelayMs, false);
	AnalysisCurve curve = buildAnalysisCurve(*response, request);
	double worst = 0.0;
	for (double value : curve.values)
		worst = std::max(worst, std::abs(value));
	expectTrue(worst < 1e-9,
		QStringLiteral("with the base delay excluded a pure delay is invisible (worst %1 ms)").arg(worst));

	request = requestFor(AnalysisMetric::GroupDelayMs, true);
	curve = buildAnalysisCurve(*response, request);
	worst = 0.0;
	for (double value : curve.values)
		worst = std::max(worst, std::abs(value - 10.0));
	expectTrue(worst < 1e-6,
		QStringLiteral("with it included the group delay is the delay itself, 10 ms (worst error %1 ms)").arg(worst));

	// The same switch on the phase view: a delay is a linear phase, and at
	// 1 kHz a 10 ms delay is exactly ten turns.
	request = requestFor(AnalysisMetric::PhaseDegrees, true);
	curve = buildAnalysisCurve(*response, request);
	const int column = columnNearest(request, 1000.0);
	const double columnHz = analysisColumnFrequency(request, column);
	const double at1k = curve.values[column];
	const double expected = -360.0 * columnHz * 0.01;
	expectTrue(std::abs(at1k - expected) < 0.5,
		QStringLiteral("a 10 ms delay is a linear phase: %1 deg expected at %2 Hz, got %3")
			.arg(expected, 0, 'f', 1).arg(columnHz, 0, 'f', 2).arg(at1k, 0, 'f', 1));

	request = requestFor(AnalysisMetric::PhaseDegrees, false);
	curve = buildAnalysisCurve(*response, request);
	worst = 0.0;
	for (double value : curve.values)
		worst = std::max(worst, std::abs(value));
	expectTrue(worst < 1e-9, QStringLiteral("with the base delay excluded the phase is flat"));
}

void testPhaseAndGroupDelayOfAnAllPass()
{
	// The filter this whole reform exists for. Flat in magnitude, so the only
	// view that can see it at all is one of these two.
	auto response = biquadResponse(BiQuad::ALL_PASS, 1000.0, 0.707);

	AnalysisCurveRequest request = requestFor(AnalysisMetric::MagnitudeDb);
	AnalysisCurve magnitude = buildAnalysisCurve(*response, request);
	double worstDb = 0.0;
	for (double value : magnitude.values)
		worstDb = std::max(worstDb, std::abs(value));
	expectTrue(worstDb < 1e-9,
		QStringLiteral("an all-pass is a flat line in magnitude (worst %1 dB)").arg(worstDb));
	expectFalse(magnitude.clipping, QStringLiteral("a flat response does not clip"));

	request = requestFor(AnalysisMetric::PhaseDegrees);
	AnalysisCurve phase = buildAnalysisCurve(*response, request);
	// Unbroken: a filter with no zeros has a phase everywhere.
	for (int i = 0; i < phase.values.size(); i++)
		requireTrue(std::isfinite(phase.values[i]),
			QStringLiteral("all-pass phase column %1 has a value").arg(i));

	// A 2nd-order all-pass passes -180 degrees at Fc and keeps turning the
	// same way. The unwrap is what makes that readable instead of a sawtooth.
	const double atFc = valueNearest(phase, request, 1000.0);
	expectTrue(std::abs(atFc + 180.0) < 2.0,
		QStringLiteral("all-pass phase is -180 deg at Fc (got %1)").arg(atFc, 0, 'f', 1));
	expectTrue(valueNearest(phase, request, 20.0) > -5.0,
		QStringLiteral("it starts near zero at the bottom of the band"));
	expectTrue(valueNearest(phase, request, 20000.0) < -300.0,
		QStringLiteral("and approaches a full turn at the top"));
	bool monotonic = true;
	for (int i = 1; i < phase.values.size(); i++)
		monotonic = monotonic && phase.values[i] <= phase.values[i - 1] + 1e-6;
	expectTrue(monotonic, QStringLiteral("the unwrapped phase only ever falls - no sawtooth survived"));

	// Group delay peaks at Fc, and raising Q makes that peak taller. This is
	// the reading the reform gives users a reason to set Q for.
	request = requestFor(AnalysisMetric::GroupDelayMs);
	const AnalysisCurve wide = buildAnalysisCurve(*response, request);
	auto sharpResponse = biquadResponse(BiQuad::ALL_PASS, 1000.0, 4.0);
	const AnalysisCurve sharp = buildAnalysisCurve(*sharpResponse, request);

	const double widePeak = valueNearest(wide, request, 1000.0);
	const double sharpPeak = valueNearest(sharp, request, 1000.0);
	// 4Q / sin(w0) samples at Fc, converted to milliseconds.
	const double omega0 = 2.0 * M_PI * 1000.0 / SampleRate;
	const double expectedWide = 4.0 * 0.707 / std::sin(omega0) / SampleRate * 1000.0;
	expectTrue(std::abs(widePeak - expectedWide) < expectedWide * 0.02,
		QStringLiteral("group delay at Fc matches 4Q/sin(w0) (expected %1 ms, got %2 ms)")
			.arg(expectedWide, 0, 'f', 3).arg(widePeak, 0, 'f', 3));
	expectTrue(sharpPeak > widePeak * 4.0,
		QStringLiteral("a higher Q concentrates more delay at Fc (%1 ms vs %2 ms)")
			.arg(sharpPeak, 0, 'f', 3).arg(widePeak, 0, 'f', 3));
	expectTrue(valueNearest(sharp, request, 100.0) < sharpPeak * 0.2,
		QStringLiteral("and the peak is narrow - a decade below Fc it has fallen away"));
}

void testPhaseBreaksWhereTheResponseIsDead()
{
	// Total cancellation, which "Copy: L=L-L" produces. There is no phase to
	// report anywhere, and the graph must say so rather than draw a line
	// through the noise floor.
	auto silent = emptyResponse();
	for (auto& bin : silent->bins)
		bin = std::complex<double>(0.0, 0.0);

	AnalysisCurveRequest request = requestFor(AnalysisMetric::PhaseDegrees);
	AnalysisCurve curve = buildAnalysisCurve(*silent, request);
	bool anyValue = false;
	for (double value : curve.values)
		anyValue = anyValue || std::isfinite(value);
	expectFalse(anyValue, QStringLiteral("a dead response reports no phase at all"));
	expectEqual(static_cast<int>(buildCurveSegments(curve.values, QRectF(0, 0, 100, 100),
		curve.minimum, curve.maximum).size()), 0,
		QStringLiteral("and there is nothing to draw"));
	expectTrue(std::isfinite(curve.minimum) && std::isfinite(curve.maximum),
		QStringLiteral("the axis stays finite rather than inheriting the missing values"));

	// One dead band inside an otherwise live response: the line breaks there
	// and resumes after, and no infinity reaches the axis fit.
	auto notched = emptyResponse();
	for (size_t i = 0; i < notched->bins.size(); i++)
	{
		const double hz = notched->frequencyOf(i);
		if (hz > 900.0 && hz < 1100.0)
			notched->bins[i] = std::complex<double>(0.0, 0.0);
	}
	curve = buildAnalysisCurve(*notched, request);
	int missing = 0;
	for (double value : curve.values)
	{
		if (!std::isfinite(value))
			missing++;
	}
	expectTrue(missing > 0, QStringLiteral("the dead band has no phase"));
	expectTrue(missing < curve.values.size(), QStringLiteral("the rest of the band still does"));
	expectTrue(static_cast<int>(buildCurveSegments(curve.values, QRectF(0, 0, 905, 100),
		curve.minimum, curve.maximum).size()) >= 2,
		QStringLiteral("so the drawn line is broken, not bridged"));

	// Group delay is differentiated across neighbouring bins, so it has to go
	// missing one bin wider than the phase does - a difference taken across
	// the hole would measure the hole.
	const AnalysisCurve delay = buildAnalysisCurve(*notched, requestFor(AnalysisMetric::GroupDelayMs));
	int delayMissing = 0;
	for (double value : delay.values)
	{
		if (!std::isfinite(value))
			delayMissing++;
	}
	expectTrue(delayMissing >= missing,
		QStringLiteral("group delay goes missing at least as widely as phase (%1 vs %2 columns)")
			.arg(delayMissing).arg(missing));
	expectTrue(std::isfinite(delay.minimum) && std::isfinite(delay.maximum),
		QStringLiteral("and the group-delay axis stays finite"));
}

void testPhaseAndGroupDelayAxisCaptions()
{
	auto response = biquadResponse(BiQuad::ALL_PASS, 1000.0, 0.707);

	AnalysisCurve phase = buildAnalysisCurve(*response, requestFor(AnalysisMetric::PhaseDegrees));
	// Degrees sit against their number; a word unit takes a space. A caller
	// that concatenated unit strings would get one of the two wrong.
	expectTrue(phase.formatValue(-181.4) == QString::fromUtf8("-181.4\xC2\xB0"),
		QStringLiteral("a phase reading is spelled -181.4 with a degree sign"));
	expectTrue(phase.formatValue(12.0) == QString::fromUtf8("+12.0\xC2\xB0"),
		QStringLiteral("a positive phase reading carries its sign"));
	expectTrue(phase.topLabel.endsWith(QString::fromUtf8("\xC2\xB0")),
		QStringLiteral("the axis captions carry the unit"));
	// The axis lands on landmark angles: a quarter turn has to be a tick.
	requireTrue(phase.ticks.size() >= 3, QStringLiteral("the phase axis has ticks"));
	const double step = phase.ticks[1].value - phase.ticks[0].value;
	expectTrue(std::abs(std::fmod(step, 15.0)) < 1e-9,
		QStringLiteral("the phase step is a whole number of 15-degree landmarks (got %1)").arg(step));
	expectTrue(std::abs(std::fmod(phase.minimum, step)) < 1e-9,
		QStringLiteral("and the axis ends land on the step"));

	AnalysisCurve delay = buildAnalysisCurve(*response, requestFor(AnalysisMetric::GroupDelayMs));
	expectTrue(delay.formatValue(0.52) == QStringLiteral("0.52 ms"),
		QStringLiteral("a group-delay reading is spelled 0.52 ms"));
	expectTrue(delay.minimum <= 0.0, QStringLiteral("the group-delay axis starts at or below zero"));
	expectTrue(delay.maximum > 0.0, QStringLiteral("and reaches above it"));
	expectFalse(delay.clipping, QStringLiteral("a long group delay is not a clip"));
	expectFalse(phase.clipping, QStringLiteral("a positive phase is not a clip"));
}
