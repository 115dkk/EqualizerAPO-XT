/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	All-pass DSP invariants and parser characterization.

	These tests were written before the All-pass reform (issue #228) touched
	anything, to pin what the engine does today. Nothing here asserts a desired
	future behaviour; every check records a fact the reform must not change, or
	a fact the reform is about to change and therefore has to be measured first.

	Two of them are characterizations of the round-trip defect rather than of
	correct behaviour:

	- testAllPassAcceptsBandwidth proves the engine and the parser have always
	  honoured "BW Oct" for AP. The Editor is the only layer that refuses it,
	  so the fix belongs there and cannot be blamed on the DSP.
	- testAllPassBandwidthIsNotTheSameNumberAsQ measures what the Editor's
	  silent "BW Oct 1 -> Q 1" rewrite actually costs. It is not a rounding
	  difference; the coefficients move far enough to hear.

	The closed forms used here are derived from the mirror-image (all-pass)
	structure of the normalized coefficients, not copied from the reform
	document, so a mistake in the document cannot pass through unnoticed.

	What this file cannot cover is the round trip itself. The rewrite happens in
	BiQuadFilterGUI, a Qt widget with a .ui, and EditorLogicTests does not link
	it - so there is nowhere to assert "open this line, save it, get it back"
	until the width-mode decision moves out of the widget into shared logic.
	That extraction is part of the Editor stage; the round-trip test lands with
	it, on the same shared function the new card calls.
*/

// Before any header that reaches <math.h>: M_PI and M_SQRT2 are only declared
// when this is defined at the point math.h is first processed, and <complex>
// gets there on its own.
#define _USE_MATH_DEFINES

#include <cmath>
#include <complex>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "filters/BiQuad.h"
#include "filters/BiQuadFilter.h"
#include "filters/BiQuadFilterFactory.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
test::Harness harness("AllPassTests");

// The reform document's grid (section 12.1).
const double kSampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
const double kQValues[] = {0.3333, 0.707, 1.0, 10.0, 33.3333};

struct Coefficients
{
	// H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), already
	// normalized by the original a0. BiQuad stores b0 separately from the
	// other four; process() applies the two denominator terms with a minus.
	double b0, b1, b2, a1, a2;
};

Coefficients coefficientsOf(const BiQuad& biquad)
{
	double packed[4];
	double b0 = 0.0;
	biquad.getCoefficients(packed, b0);
	return Coefficients{b0, packed[0], packed[1], packed[2], packed[3]};
}

BiQuad makeAllPass(double freq, double srate, double widthValue, bool isBandwidth)
{
	return BiQuad(BiQuad::ALL_PASS, 0.0, freq, srate, widthValue, isBandwidth);
}

std::complex<double> responseAt(const Coefficients& c, double freq, double srate)
{
	const double omega = 2.0 * M_PI * freq / srate;
	const std::complex<double> z1 = std::polar(1.0, -omega);
	const std::complex<double> z2 = z1 * z1;
	return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

double phaseDegreesAt(const Coefficients& c, double freq, double srate)
{
	return std::arg(responseAt(c, freq, srate)) * 180.0 / M_PI;
}

// Group delay in samples, differentiated in closed form.
//
// A finite difference is the wrong tool here twice over. std::arg wraps into
// (-pi, pi] and Fc is exactly where a 2nd-order all-pass sits at -180 degrees,
// so the two sample points straddle the branch cut; and once that is folded
// back, the truncation error of the central difference grows with Q, because
// the phase transition narrows as the delay peak sharpens. At Q 33 the answer
// was already wrong in the fifth digit.
//
// For a polynomial P in z^-1, d/dw arg(P) is Im(P'/P), where P' differentiates
// term by term. So the group delay is Im(D'/D) - Im(N'/N), exactly, at any Q,
// with no step size to tune and no wrapping to undo. It also stays correct for
// a 1st-order section, where b2 and a2 are zero.
double groupDelaySamplesAt(const Coefficients& c, double freq, double srate)
{
	const std::complex<double> j(0.0, 1.0);
	const std::complex<double> z1 = std::polar(1.0, -2.0 * M_PI * freq / srate);
	const std::complex<double> z2 = z1 * z1;

	const std::complex<double> numerator = c.b0 + c.b1 * z1 + c.b2 * z2;
	const std::complex<double> denominator = 1.0 + c.a1 * z1 + c.a2 * z2;
	const std::complex<double> dNumerator = -j * (c.b1 * z1 + 2.0 * c.b2 * z2);
	const std::complex<double> dDenominator = -j * (c.a1 * z1 + 2.0 * c.a2 * z2);

	return (dDenominator / denominator).imag() - (dNumerator / numerator).imag();
}

bool allFinite(const Coefficients& c)
{
	return std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2)
		&& std::isfinite(c.a1) && std::isfinite(c.a2);
}

std::string describe(double srate, double q)
{
	std::ostringstream oss;
	oss << "srate " << srate << ", Q " << q;
	return oss.str();
}

// An all-pass is flat by construction, not by approximation: the normalized
// numerator is the reverse of the denominator, so |H| is exactly 1 at every
// frequency. A magnitude that drifts means the coefficient derivation broke,
// not that the tolerance was too tight.
void testMagnitudeIsFlatAcrossTheBand()
{
	for (double srate : kSampleRates)
	{
		for (double q : kQValues)
		{
			BiQuad biquad = makeAllPass(1000.0, srate, q, false);
			double worst = 0.0;
			double worstHz = 0.0;
			// 20 Hz up to just short of Nyquist; the endpoint itself is a
			// degenerate case for every biquad, not just this one.
			for (double hz = 20.0; hz < srate * 0.499; hz *= 1.05)
			{
				const double db = std::abs(biquad.gainAt(hz, srate));
				if (db > worst)
				{
					worst = db;
					worstHz = hz;
				}
			}
			std::ostringstream oss;
			oss << "all-pass magnitude stays at 0 dB (" << describe(srate, q)
				<< ", worst " << worst << " dB at " << worstHz << " Hz)";
			harness.expect(worst < 1e-9, oss.str());
		}
	}
}

// The mirror-image structure itself, checked directly. b2 is exactly 1 and the
// numerator is the denominator read backwards; this is the property the flat
// magnitude follows from, so pinning it says more than pinning the magnitude.
void testCoefficientsAreFiniteAndMirrored()
{
	for (double srate : kSampleRates)
	{
		for (double q : kQValues)
		{
			const Coefficients c = coefficientsOf(makeAllPass(1000.0, srate, q, false));
			harness.expect(allFinite(c), "all-pass coefficients are finite, " + describe(srate, q));
			harness.expect(std::abs(c.b2 - 1.0) < 1e-12,
				"all-pass b2 is 1, " + describe(srate, q));
			harness.expect(std::abs(c.b1 - c.a1) < 1e-12,
				"all-pass b1 mirrors a1, " + describe(srate, q));
			harness.expect(std::abs(c.b0 - c.a2) < 1e-12,
				"all-pass b0 mirrors a2, " + describe(srate, q));
			// Stability: both poles inside the unit circle. For a real biquad
			// that is |a2| < 1 and |a1| < 1 + a2.
			harness.expect(std::abs(c.a2) < 1.0 && std::abs(c.a1) < 1.0 + c.a2,
				"all-pass poles stay inside the unit circle, " + describe(srate, q));
		}
	}
}

// A 2nd-order all-pass turns a full circle: 0 degrees at DC, -180 at Fc,
// -360 approaching Nyquist. The middle value is what a user sets Fc for, so
// it is the one worth pinning.
void testPhaseAtCenterIsHalfATurn()
{
	for (double srate : kSampleRates)
	{
		for (double q : kQValues)
		{
			const double fc = 1000.0;
			const Coefficients c = coefficientsOf(makeAllPass(fc, srate, q, false));

			const double atCenter = phaseDegreesAt(c, fc, srate);
			std::ostringstream oss;
			oss << "all-pass phase at Fc is -180 deg (" << describe(srate, q)
				<< ", got " << atCenter << ")";
			harness.expect(std::abs(std::abs(atCenter) - 180.0) < 1e-6, oss.str());

			harness.expect(std::abs(phaseDegreesAt(c, 1.0, srate)) < 1.0,
				"all-pass phase approaches 0 deg at DC, " + describe(srate, q));
		}
	}
}

// Group delay at Fc, in samples, is exactly 4Q / sin(w0). The familiar
// 2Q / (pi * Fc) seconds is the w0 << 1 limit of that and drifts upward as Fc
// climbs, so the exact form is what gets asserted; the approximation is only
// checked where it is meant to hold.
void testGroupDelayAtCenterMatchesTheClosedForm()
{
	for (double srate : kSampleRates)
	{
		for (double q : kQValues)
		{
			const double fc = 200.0;
			const Coefficients c = coefficientsOf(makeAllPass(fc, srate, q, false));

			const double omega0 = 2.0 * M_PI * fc / srate;
			const double measured = groupDelaySamplesAt(c, fc, srate);
			const double expected = 4.0 * q / std::sin(omega0);
			std::ostringstream oss;
			oss << "all-pass group delay at Fc is 4Q/sin(w0) samples (" << describe(srate, q)
				<< ", expected " << expected << ", measured " << measured << ")";
			harness.expect(std::abs(measured - expected) < expected * 1e-6, oss.str());

			// The textbook 2Q/(pi*Fc) seconds only holds while w0 is small;
			// at 200 Hz it is within a part in 10^4 at every rate here.
			const double approxSeconds = 2.0 * q / (M_PI * fc);
			harness.expect(std::abs(measured / srate - approxSeconds) < approxSeconds * 1e-3,
				"the 2Q/(pi*Fc) approximation holds at low Fc, " + describe(srate, q));
		}
	}
}

void renderInBlocks(BiQuadFilter& filter, unsigned totalFrames, unsigned blockFrames, vector<double>& out)
{
	vector<wstring> names(1, wstring(L"C0"));
	filter.initialize(48000.0f, blockFrames, names);

	out.assign(totalFrames, 0.0);
	vector<double> input(blockFrames);
	vector<double> output(blockFrames);
	double* inPtr[1] = {input.data()};
	double* outPtr[1] = {output.data()};

	// Impulse followed by silence: the whole tail of an IIR is in the silence,
	// which is exactly where a mishandled block boundary would show up.
	unsigned written = 0;
	while (written < totalFrames)
	{
		for (unsigned i = 0; i < blockFrames; i++)
			// cppcheck-suppress unreadVariable ; process() reads this through inPtr[0], which was
			// bound to input.data() before the loop - an indirection cppcheck's dataflow does not follow
			input[i] = (written + i == 0) ? 1.0 : 0.0;
		filter.process(outPtr, inPtr, blockFrames);
		memcpy(out.data() + written, output.data(), blockFrames * sizeof(double));
		written += blockFrames;
	}
}

// The engine is driven in fixed-size blocks by Windows, and the block size is
// not ours to choose. An all-pass carries two samples of state across the
// boundary, so a different block size has to produce the same stream bit for
// bit.
void testOutputIsBlockSizeInvariant()
{
	const unsigned totalFrames = 4096;
	vector<double> reference;
	{
		BiQuadFilter filter(BiQuad::ALL_PASS, 0.0, 1000.0, 0.707, false, false);
		renderInBlocks(filter, totalFrames, totalFrames, reference);
	}

	for (unsigned blockFrames : {1u, 16u, 64u, 512u})
	{
		BiQuadFilter filter(BiQuad::ALL_PASS, 0.0, 1000.0, 0.707, false, false);
		vector<double> blocked;
		renderInBlocks(filter, totalFrames, blockFrames, blocked);

		std::ostringstream oss;
		oss << "all-pass output is identical in blocks of " << blockFrames;
		harness.expect(memcmp(reference.data(), blocked.data(), totalFrames * sizeof(double)) == 0,
			oss.str());
	}
}

// The multi-channel path picks a SIMD, dual or scalar kernel by channel count.
// BiQuadKernelTests proves that split is exhaustive for a peaking filter; this
// repeats the equivalence for an all-pass, whose numerator is the only part
// that differs and whose coefficients are the least well conditioned of the
// family at high Q.
void testKernelsAgreeForAllPass()
{
	const wstring names[] = {L"C0", L"C1", L"C2", L"C3", L"C4", L"C5", L"C6", L"C7"};
	const unsigned frames = 480;

	for (unsigned channelCount : {2u, 3u})
	{
		BiQuadFilter multi(BiQuad::ALL_PASS, 0.0, 997.0, 10.0, false, false);
		vector<wstring> multiNames(names, names + channelCount);
		multi.initialize(48000.0f, frames, multiNames);

		vector<BiQuadFilter> monos;
		monos.reserve(channelCount);
		for (unsigned c = 0; c < channelCount; c++)
		{
			monos.emplace_back(BiQuad::ALL_PASS, 0.0, 997.0, 10.0, false, false);
			vector<wstring> monoName(1, names[c]);
			monos.back().initialize(48000.0f, frames, monoName);
		}

		vector<double> inputData((size_t)channelCount * frames);
		vector<double> multiOut((size_t)channelCount * frames);
		vector<double> monoOut((size_t)channelCount * frames);
		vector<double*> inputPtrs(channelCount);
		vector<double*> outputPtrs(channelCount);

		unsigned long long state = 0x9E3779B97F4A7C15ULL;
		for (unsigned c = 0; c < channelCount; c++)
		{
			inputPtrs[c] = inputData.data() + (size_t)c * frames;
			outputPtrs[c] = multiOut.data() + (size_t)c * frames;
			for (unsigned i = 0; i < frames; i++)
			{
				state ^= state >> 12;
				state ^= state << 25;
				state ^= state >> 27;
				const unsigned long long scrambled = state * 2685821657736338717ULL;
				inputPtrs[c][i] = static_cast<double>(scrambled >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
			}
		}
		multi.process(outputPtrs.data(), inputPtrs.data(), frames);

		for (unsigned c = 0; c < channelCount; c++)
		{
			double* outPtr[1] = {monoOut.data() + (size_t)c * frames};
			double* inPtr[1] = {inputPtrs[c]};
			monos[c].process(outPtr, inPtr, frames);
		}

		std::ostringstream oss;
		oss << "all-pass " << channelCount << "-channel output matches the mono filters bit for bit";
		harness.expect(memcmp(multiOut.data(), monoOut.data(), multiOut.size() * sizeof(double)) == 0,
			oss.str());
	}
}

// Fc pushed against either end of the band. The poles approach the unit circle
// there, which is where a filter stops being usable if it ever does; the
// output still has to be finite and the magnitude still has to be flat.
void testExtremeCenterFrequenciesStayStable()
{
	const double srate = 48000.0;
	for (double fc : {1.0, 10.0, 20.0, 20000.0, 23000.0, 23990.0})
	{
		for (double q : kQValues)
		{
			const Coefficients c = coefficientsOf(makeAllPass(fc, srate, q, false));
			std::ostringstream label;
			label << "Fc " << fc << " Hz, Q " << q;

			harness.expect(allFinite(c), "coefficients stay finite at " + label.str());
			harness.expect(std::abs(c.a2) < 1.0, "poles stay inside the unit circle at " + label.str());

			BiQuadFilter filter(BiQuad::ALL_PASS, 0.0, fc, q, false, false);
			vector<double> out;
			renderInBlocks(filter, 2048, 256, out);
			bool finite = true;
			for (double sample : out)
				finite = finite && std::isfinite(sample);
			harness.expect(finite, "output stays finite at " + label.str());
		}
	}
}

// The engine has always accepted "BW Oct" for an all-pass: BiQuad's alpha
// branch for bandwidth does not look at the filter type at all. Only the
// Editor refuses it. Pinning that here keeps the fix on the Editor side.
void testAcceptsBandwidth()
{
	wstring parameters = L"ON AP Fc 80 Hz BW Oct 1";
	BiQuadCommand command;
	harness.require(BiQuadFilterFactory::parseCommand(L"Filter", parameters, command),
		"the parser accepts BW Oct for an all-pass");
	harness.expectEqual(static_cast<int>(command.type), static_cast<int>(BiQuad::ALL_PASS),
		"BW Oct line parses as an all-pass");
	harness.expectTrue(command.isBandwidthOrS, "the all-pass width is flagged as a bandwidth");
	harness.expect(std::abs(command.bandwidthOrQOrS - 1.0) < 1e-12, "the bandwidth value survives the parse");
}

// What the Editor's silent rewrite costs. Opening "BW Oct 1" and saving turns
// it into "Q 1" today, because the width-mode selector offers the all-pass no
// second entry to restore. The two are not the same filter, and the gap is
// large enough to hear: this records how large before the fix removes it.
void testBandwidthIsNotTheSameNumberAsQ()
{
	const double srate = 48000.0;
	const double fc = 80.0;
	const Coefficients asBandwidth = coefficientsOf(makeAllPass(fc, srate, 1.0, true));
	const Coefficients asQ = coefficientsOf(makeAllPass(fc, srate, 1.0, false));

	harness.expect(std::abs(asBandwidth.a1 - asQ.a1) > 1e-6 || std::abs(asBandwidth.a2 - asQ.a2) > 1e-6,
		"BW Oct 1 and Q 1 are different all-pass filters");

	// Group delay at Fc is where the difference lands, and it is the value the
	// whole feature exists to control. At 80 Hz a 1-octave bandwidth is very
	// nearly Q 1.414, so reading the number as a Q instead shortens the delay
	// by that factor.
	const double ratio = groupDelaySamplesAt(asBandwidth, fc, srate)
		/ groupDelaySamplesAt(asQ, fc, srate);
	std::ostringstream oss;
	oss << "rewriting BW Oct 1 as Q 1 changes the group delay at Fc by about 41% (ratio " << ratio << ")";
	harness.expect(std::abs(ratio - M_SQRT2) < 0.01, oss.str());
}

// Two 1st-order sections at the same frequency, in series, are exactly one
// 2nd-order section at Q 0.5. Not approximately - the normalized coefficients
// are equal term for term, because c1 = 2c and c2 = c^2 both fall out of
// c = (t-1)/(t+1) with t = tan(w0/2).
//
// This is the harness for the whole 1st-order addition. A mistake in the
// bilinear derivation or in the prewarping shows up here, tied to a filter
// whose behaviour is already pinned by every test above. If it fails, the
// derivation is wrong; do not go adjusting the other 1st-order tests.
void testTwoFirstOrderSectionsEqualOneSecondOrderAtQHalf()
{
	for (double srate : kSampleRates)
	{
		for (double fc : {20.0, 100.0, 1000.0, 10000.0})
		{
			if (fc >= srate * 0.45)
				continue;

			const Coefficients first = coefficientsOf(BiQuad(BiQuad::ALL_PASS_1, 0.0, fc, srate, 0.0, false));
			const Coefficients second = coefficientsOf(makeAllPass(fc, srate, 0.5, false));

			std::ostringstream label;
			label << "srate " << srate << ", Fc " << fc << " Hz";

			// The cascade's transfer function is the square of the section's:
			// (c + z^-1)^2 / (1 + c z^-1)^2.
			const double c = first.b0;
			harness.expect(std::abs(first.b1 - 1.0) < 1e-12, "the 1st-order numerator is c + z^-1, " + label.str());
			harness.expect(first.b2 == 0.0 && first.a2 == 0.0,
				"the 1st-order section leaves the second-order taps at exactly zero, " + label.str());
			harness.expect(std::abs(first.a1 - c) < 1e-12, "the 1st-order denominator mirrors it, " + label.str());

			harness.expect(std::abs(second.a1 - 2.0 * c) < 1e-12,
				"two 1st-order sections give the Q 0.5 first-order term, " + label.str());
			harness.expect(std::abs(second.a2 - c * c) < 1e-12,
				"and its second-order term, " + label.str());

			// The same statement as a response check, at a frequency where the
			// two would visibly disagree if the derivation were off.
			// The response comparison is looser than the coefficient one on
			// purpose. At 20 Hz on a 192 kHz stream the pole sits within 7e-4
			// of z = 1, so evaluating there magnifies the last bits of the
			// coefficients; the tolerance is what that conditioning costs, not
			// slack in the identity. |H| is 1 everywhere, so this is a relative
			// error too.
			const double probe = fc * 1.7;
			const std::complex<double> cascade = responseAt(first, probe, srate) * responseAt(first, probe, srate);
			const std::complex<double> single = responseAt(second, probe, srate);
			harness.expect(std::abs(cascade - single) < 1e-9,
				"the cascade's response equals the Q 0.5 section's, " + label.str());
		}
	}
}

// A 1st-order section turns half as far as a 2nd-order one and passes -90
// degrees at Fc rather than -180. That half turn is what no cascade of
// 2nd-order sections can produce, and the whole reason the order exists.
void testFirstOrderAllPassBehaviour()
{
	for (double srate : kSampleRates)
	{
		for (double fc : {20.0, 100.0, 1000.0, 10000.0, srate * 0.45})
		{
			const Coefficients c = coefficientsOf(BiQuad(BiQuad::ALL_PASS_1, 0.0, fc, srate, 0.0, false));
			std::ostringstream label;
			label << "srate " << srate << ", Fc " << fc << " Hz";

			harness.expect(allFinite(c), "1st-order coefficients are finite, " + label.str());
			harness.expect(std::abs(c.b0) < 1.0, "the 1st-order pole stays inside the unit circle, " + label.str());

			BiQuad magnitude(BiQuad::ALL_PASS_1, 0.0, fc, srate, 0.0, false);
			double worst = 0.0;
			for (double hz = 20.0; hz < srate * 0.499; hz *= 1.1)
				worst = std::max(worst, std::abs(magnitude.gainAt(hz, srate)));
			harness.expect(worst < 1e-9, "1st-order magnitude stays at 0 dB, " + label.str());

			const double atCenter = phaseDegreesAt(c, fc, srate);
			std::ostringstream oss;
			oss << "1st-order phase at Fc is -90 deg (" << label.str() << ", got " << atCenter << ")";
			harness.expect(std::abs(atCenter + 90.0) < 1e-6, oss.str());

			// Probed relative to Fc rather than at a fixed 1 Hz: a 1st-order
			// section turns gradually, so at 1 Hz a filter centred on 20 Hz is
			// already several degrees along. Four decades below Fc the phase is
			// about -0.01 degrees.
			harness.expect(std::abs(phaseDegreesAt(c, fc * 1e-4, srate)) < 0.1,
				"1st-order phase approaches 0 deg well below Fc, " + label.str());
			const double nearNyquist = phaseDegreesAt(c, srate * 0.4999, srate);
			harness.expect(nearNyquist < -175.0 && nearNyquist >= -180.0,
				"1st-order phase approaches -180 deg at Nyquist and no further, " + label.str());

			// Group delay at Fc, in samples, is (1 + K^2) / (2K).
			const double K = std::tan(M_PI * fc / srate);
			const double expected = (1.0 + K * K) / (2.0 * K);
			const double measured = groupDelaySamplesAt(c, fc, srate);
			std::ostringstream gd;
			gd << "1st-order group delay at Fc is (1+K^2)/(2K) samples (" << label.str()
				<< ", expected " << expected << ", measured " << measured << ")";
			harness.expect(std::abs(measured - expected) < expected * 1e-6, gd.str());
		}
	}
}

// The grammar the order arrives in.
void testOrderParsing()
{
	auto parse = [](const wchar_t* text, BiQuadCommand& out) {
		std::wstring parameters(text);
		return BiQuadFilterFactory::parseCommand(L"Filter", parameters, out);
	};

	BiQuadCommand command;
	// No Order at all is a 2nd-order section. Every configuration written
	// before the order existed depends on this reading.
	harness.require(parse(L"ON AP Fc 100 Hz Q 0.707", command), "an all-pass without an order parses");
	harness.expectEqual(static_cast<int>(command.type), static_cast<int>(BiQuad::ALL_PASS),
		"an all-pass without an order is 2nd order");
	harness.expectFalse(command.orderWasExplicit, "and the line is marked as not having said so");

	harness.require(parse(L"ON AP Fc 100 Hz Q 0.707 Order 2", command), "Order 2 parses");
	harness.expectEqual(static_cast<int>(command.type), static_cast<int>(BiQuad::ALL_PASS),
		"Order 2 is the 2nd-order section");
	harness.expectTrue(command.orderWasExplicit, "and the line is marked as having said so");

	harness.require(parse(L"ON AP Fc 100 Hz Order 1", command), "Order 1 parses without a width");
	harness.expectEqual(static_cast<int>(command.type), static_cast<int>(BiQuad::ALL_PASS_1),
		"Order 1 is the 1st-order section");
	harness.expectTrue(command.orderWasExplicit, "Order 1 is an explicit order");

	// A width left over from a 2nd-order line is discarded rather than
	// rejected: switching an existing filter's order should not make its line
	// unloadable.
	BiQuadCommand withWidth;
	BiQuadCommand withoutWidth;
	harness.require(parse(L"ON AP Fc 250 Hz Q 2 Order 1", withWidth), "Order 1 with a stray Q still parses");
	harness.require(parse(L"ON AP Fc 250 Hz Order 1", withoutWidth), "Order 1 without one parses");
	harness.expect(withWidth.bandwidthOrQOrS == 0.0, "the stray width is discarded");
	const Coefficients a = coefficientsOf(BiQuad(withWidth.type, 0.0, withWidth.freq, 48000.0,
		withWidth.bandwidthOrQOrS, withWidth.isBandwidthOrS));
	const Coefficients b = coefficientsOf(BiQuad(withoutWidth.type, 0.0, withoutWidth.freq, 48000.0,
		withoutWidth.bandwidthOrQOrS, withoutWidth.isBandwidthOrS));
	harness.expect(a.b0 == b.b0 && a.b1 == b.b1 && a.a1 == b.a1,
		"so it makes exactly the filter it would without one");

	harness.expectFalse(parse(L"ON AP Fc 100 Hz Order 3", command), "Order 3 is rejected");
	harness.expectFalse(parse(L"ON AP Fc 100 Hz Order 0", command), "Order 0 is rejected");

	// The order is meaningless on the other biquad types and is ignored there
	// rather than changing what they are.
	harness.require(parse(L"ON PK Fc 1000 Hz Gain 3 dB Q 1 Order 1", command),
		"an order on a peaking filter still parses");
	harness.expectEqual(static_cast<int>(command.type), static_cast<int>(BiQuad::PEAKING),
		"and leaves it a peaking filter");
}

// An all-pass with no width is an error, unlike LP/HP/BP/NO which fall back to
// a default. The reform keeps that for Order 2 and relaxes it only for Order 1,
// where the parameter does not exist.
void testWidthIsMandatory()
{
	wstring parameters = L"ON AP Fc 100 Hz";
	BiQuadCommand command;
	harness.expectFalse(BiQuadFilterFactory::parseCommand(L"Filter", parameters, command),
		"an all-pass without Q or BW is rejected");
}

// Gain is meaningless for a filter that is flat by construction, and the
// parser says so in the trace rather than applying it.
void testGainIsIgnored()
{
	wstring withGain = L"ON AP Fc 100 Hz Gain 6 dB Q 0.707";
	wstring withoutGain = L"ON AP Fc 100 Hz Q 0.707";
	BiQuadCommand a, b;
	harness.require(BiQuadFilterFactory::parseCommand(L"Filter", withGain, a), "gain-carrying all-pass parses");
	harness.require(BiQuadFilterFactory::parseCommand(L"Filter", withoutGain, b), "plain all-pass parses");
	harness.expect(a.dbGain == 0.0, "the all-pass gain token is discarded");
	harness.expect(a.dbGain == b.dbGain && a.freq == b.freq && a.bandwidthOrQOrS == b.bandwidthOrQOrS,
		"a gain token does not change the all-pass filter");
}
}

void runAllPassTests()
{
	testMagnitudeIsFlatAcrossTheBand();
	testCoefficientsAreFiniteAndMirrored();
	testPhaseAtCenterIsHalfATurn();
	testGroupDelayAtCenterMatchesTheClosedForm();
	testOutputIsBlockSizeInvariant();
	testKernelsAgreeForAllPass();
	testExtremeCenterFrequenciesStayStable();
	testTwoFirstOrderSectionsEqualOneSecondOrderAtQHalf();
	testFirstOrderAllPassBehaviour();
	testOrderParsing();
	testAcceptsBandwidth();
	testBandwidthIsNotTheSameNumberAsQ();
	testWidthIsMandatory();
	testGainIsIgnored();

	harness.report();
}
