/***************************************************************************
 *   Copyright (C) 2009 by Christian Borss                                 *
 *   christian.borss@rub.de                                                *
 *                                                                         *
 *   This program is LGPL-licensed software; you can redistribute it and/or modify *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
 // Adapted version for Equalizer APO; see libHybridConv_eapo.h for the
 // provenance note (the vendored originals were removed in audit #250 F025).

#include "stdafx.h"
#ifndef _M_ARM64
#include <immintrin.h>   // only for _mm_prefetch; vector math goes through Highway
#endif
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string.h>
#include <unordered_map>
#include <vector>
#ifdef WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#endif
#include <math.h>
#include <fftw3.h>
#include "../dsp/FftwRAII.h"
#include "../dsp/FftwPlanningPolicy.h"
#include "../services/logging/LogHelper.h"
#include "HcAlignedStorage.h"
#include "libHybridConv_eapo.h"

#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

// The transformed impulse response is immutable after initialization and can
// be shared by several channel instances. Runtime state and FFTW plans remain
// instance-owned because hcProcessSingle mutates them on the audio thread.
struct HConvFilterBankStorage
{
	HcSlabPtr<double> slab;
	std::vector<double*> realPtrs;
	std::vector<double*> imagPtrs;
	int frameLength = 0;
	int segmentCount = 0;
	std::size_t planeStride = 0;
};

struct HConvSingleStorage
{
	std::shared_ptr<const HConvFilterBankStorage> filterBank;
	HcAlignedPtr<double> dftTime;
	HcAlignedPtr<fftw_complex> dftFreq;
	HcAlignedPtr<double> inFreqReal;
	HcAlignedPtr<double> inFreqImag;
	std::vector<int> stepTask;
	// Mix planes are mutable per-channel state. The immutable filter planes live
	// in filterBank; both layouts are pinned by HybridConvTests.
	HcSlabPtr<double> mixSlab;
	std::vector<double*> mixRealPtrs;
	std::vector<double*> mixImagPtrs;
	HcAlignedPtr<double> historyTime;
};

double hcTime(void)
{
#ifdef WIN32
	ULONGLONG t = GetTickCount64();
	return static_cast<double>(t) * 0.001;
#else
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return tv.tv_sec + tv.tv_usec * 0.000001;
#endif
}

////////////////////////////////////////////////////////////////

void hcPutSingle(HConvSingle* filter, double* x)
{
	const size_t flen = (size_t)filter->framelength;
	const size_t dft_len = 2 * flen;
	const size_t freq_len = flen + 1;

	// --- Phase 1: Input Preparation (copy x[0..flen-1], zero-pad [flen..2*flen-1]) ---
	const hn::ScalableTag<double> d;
	const size_t N = hn::Lanes(d);
	size_t n = 0;

	{
		const auto zero_vec = hn::Zero(d);
		for (; n + N <= flen; n += N)
			hn::StoreU(hn::LoadU(d, x + n), d, filter->dft_time + n);
		// Only zero-pad once the whole input has been copied.
		if (n >= flen) {
			for (; n + N <= dft_len; n += N)
				hn::StoreU(zero_vec, d, filter->dft_time + n);
		}
	}

	if (n < flen) {
		memcpy(filter->dft_time + n, x + n, (flen - n) * sizeof *filter->dft_time);
		n = flen;
	}
	if (n < dft_len) {
		memset(filter->dft_time + n, 0, (dft_len - n) * sizeof *filter->dft_time);
	}

	// --- Phase 2: FFT ---
	fftw_execute(filter->fft);

	// --- Phase 3: De-interleave FFTW complex output into planar real/imag ---
	size_t j = 0;
	fftw_complex* dft_freq = filter->dft_freq;
	const double* dft_freq_d = reinterpret_cast<const double*>(dft_freq);

	// Planar split via one interleaved load: [r0 i0 r1 i1 ...] -> re[], im[].
	// LoadInterleaved2 is a pure shuffle, so this is bit-identical on every target.
	for (; j + N <= freq_len; j += N) {
		hn::Vec<decltype(d)> vr, vi;
		hn::LoadInterleaved2(d, dft_freq_d + j * 2, vr, vi);
		hn::StoreU(vr, d, filter->in_freq_real + j);
		hn::StoreU(vi, d, filter->in_freq_imag + j);
	}

	// Scalar tail (<1 complex for r2c)
	for (; j < freq_len; ++j) {
		filter->in_freq_real[j] = dft_freq[j][0];
		filter->in_freq_imag[j] = dft_freq[j][1];
	}
}


void hcProcessSingle(HConvSingle* filter)
{
	const int flen = filter->framelength;
	// Arrays hold flen+1 doubles (DC..Nyquist)
	const size_t num_elements = (size_t)flen + 1;

	const double* const x_real = filter->in_freq_real;
	const double* const x_imag = filter->in_freq_imag;

	const int start = filter->steptask[filter->step];
	const int stop = filter->steptask[filter->step + 1];

	for (int s = start; s < stop; ++s) {
		const int mix_idx = (s + filter->mixpos) % filter->num_mixbuf;

		double* const       y_real = filter->mixbuf_freq_real[mix_idx];
		double* const       y_imag = filter->mixbuf_freq_imag[mix_idx];
		const double* const h_real = filter->filterbuf_freq_real[s];
		const double* const h_imag = filter->filterbuf_freq_imag[s];

#if !defined(_M_ARM64)
		// Prefetch next filter segment to help hide memory latency.
		// For large filter banks, prefetch 2 segments ahead for better performance.
		const int prefetch_distance = (stop - start > 4) ? 2 : 1;
		if (s + prefetch_distance < stop) {
			_mm_prefetch(reinterpret_cast<const char*>(filter->filterbuf_freq_real[s + prefetch_distance]), _MM_HINT_T0);
			_mm_prefetch(reinterpret_cast<const char*>(filter->filterbuf_freq_imag[s + prefetch_distance]), _MM_HINT_T0);
		}
#endif

		const hn::ScalableTag<double> d;
		const size_t N = hn::Lanes(d);
		size_t n = 0;

		// Complex multiply-accumulate, one portable loop in place of the old
		// AVX-512/AVX2/SSE2 copies. MulAdd(a,b,c)=a*b+c and NegMulAdd(a,b,c)=c-a*b
		// keep the exact op order of the former fmadd/fnmadd sequence, so the
		// result is bit-identical on x86 and now runs on NEON for ARM64.
		for (; n + N <= num_elements; n += N) {
			const auto xr = hn::LoadU(d, x_real + n);
			const auto xi = hn::LoadU(d, x_imag + n);
			const auto hr = hn::LoadU(d, h_real + n);
			const auto hi = hn::LoadU(d, h_imag + n);

			auto yr = hn::LoadU(d, y_real + n);
			auto yi = hn::LoadU(d, y_imag + n);

			// Real: yr += xr*hr - xi*hi
			yr = hn::MulAdd(xr, hr, yr);     // yr = xr*hr + yr
			yr = hn::NegMulAdd(xi, hi, yr);  // yr = yr - xi*hi

			// Imag: yi += xr*hi + xi*hr
			yi = hn::MulAdd(xr, hi, yi);     // yi = xr*hi + yi
			yi = hn::MulAdd(xi, hr, yi);     // yi = xi*hr + yi

			hn::StoreU(yr, d, y_real + n);
			hn::StoreU(yi, d, y_imag + n);
		}

		// Scalar tail (and works for ARM64 too).
		for (; n < num_elements; ++n) {
			y_real[n] += x_real[n] * h_real[n] - x_imag[n] * h_imag[n];
			y_imag[n] += x_real[n] * h_imag[n] + x_imag[n] * h_real[n];
		}
	}

	filter->step = (filter->step + 1) % filter->maxstep;
}

static inline void zero_doubles_simd(double* __restrict p, int len)
{
	const hn::ScalableTag<double> d;
	const int N = (int)hn::Lanes(d);
	const auto z = hn::Zero(d);
	int i = 0;
	for (; i + N <= len; i += N)
		hn::StoreU(z, d, p + i);
	for (; i < len; ++i) p[i] = 0.0;
}

static inline void add_out_hist_to_y_simd(const double* __restrict out,
	const double* __restrict hist,
	double* __restrict y,
	int len,
	int add_to_existing_y /*0: assign; 1: += */)
{
	const hn::ScalableTag<double> d;
	const int N = (int)hn::Lanes(d);
	int i = 0;
	if (add_to_existing_y) {
		for (; i + N <= len; i += N) {
			const auto s = hn::Add(hn::LoadU(d, out + i), hn::LoadU(d, hist + i));
			hn::StoreU(hn::Add(s, hn::LoadU(d, y + i)), d, y + i);
		}
	}
	else {
		for (; i + N <= len; i += N) {
			const auto s = hn::Add(hn::LoadU(d, out + i), hn::LoadU(d, hist + i));
			hn::StoreU(s, d, y + i);
		}
	}
	for (; i < len; ++i) {
		double s = out[i] + hist[i];
		y[i] = add_to_existing_y ? (y[i] + s) : s;
	}
}

static inline void copy_hist_from_out_tail_simd(double* __restrict hist,
	const double* __restrict out_tail,
	int len)
{
	const hn::ScalableTag<double> d;
	const int N = (int)hn::Lanes(d);
	int i = 0;
	for (; i + N <= len; i += N)
		hn::StoreU(hn::LoadU(d, out_tail + i), d, hist + i);
	for (; i < len; ++i) hist[i] = out_tail[i];
}

void hcGetSingle(HConvSingle* filter, double* y)
{
	int flen = filter->framelength;
	int mpos = filter->mixpos;

	const double* out = filter->dft_time;        // length = 2*flen
	double* hist = filter->history_time;    // length = flen

	// Move one frequency frame from mixbuf -> dft_freq and zero the source.
	// Keep scalar here to preserve exact per-bin assignment order into AoS fftw_complex.
	for (int j = 0; j < flen + 1; ++j)
	{
		filter->dft_freq[j][0] = filter->mixbuf_freq_real[mpos][j];
		filter->dft_freq[j][1] = filter->mixbuf_freq_imag[mpos][j];
	}

	// Zero the mix buffers for this slot (vectorized).
	zero_doubles_simd(filter->mixbuf_freq_real[mpos], flen + 1);
	zero_doubles_simd(filter->mixbuf_freq_imag[mpos], flen + 1);

	// IFFT (unchanged).
	fftw_execute(filter->ifft);

	// Time-domain overlap-add: y[n] = out[n] + hist[n]   (vectorized).
	add_out_hist_to_y_simd(/*out:*/ out,
		/*hist:*/ hist,
		/*y:*/ y,
		/*len:*/ flen,
		/*add_to_existing_y:*/ 0);

	// Update history with tail: hist <- out[flen .. 2*flen-1] (vectorized).
	copy_hist_from_out_tail_simd(hist, out + flen, flen);

	// Advance circular position.
	filter->mixpos = (mpos + 1) % filter->num_mixbuf;
}

void hcGetAddSingle(HConvSingle* filter, double* y)
{
	int flen = filter->framelength;
	int mpos = filter->mixpos;

	const double* out = filter->dft_time;        // length = 2*flen
	double* hist = filter->history_time;    // length = flen

	// Move one frequency frame from mixbuf -> dft_freq and zero the source.
	for (int j = 0; j < flen + 1; ++j)
	{
		filter->dft_freq[j][0] = filter->mixbuf_freq_real[mpos][j];
		filter->dft_freq[j][1] = filter->mixbuf_freq_imag[mpos][j];
	}

	zero_doubles_simd(filter->mixbuf_freq_real[mpos], flen + 1);
	zero_doubles_simd(filter->mixbuf_freq_imag[mpos], flen + 1);

	fftw_execute(filter->ifft);

	// Accumulate: y[n] += out[n] + hist[n]   (vectorized).
	add_out_hist_to_y_simd(/*out:*/ out,
		/*hist:*/ hist,
		/*y:*/ y,
		/*len:*/ flen,
		/*add_to_existing_y:*/ 1);

	// Update history with tail.
	copy_hist_from_out_tail_simd(hist, out + flen, flen);

	filter->mixpos = (mpos + 1) % filter->num_mixbuf;
}

static inline void mul_store_gain_double(double* __restrict dst,
	const double* __restrict src,
	int n, double gain)
{
	const hn::ScalableTag<double> d;
	const int N = (int)hn::Lanes(d);
	const auto g = hn::Set(d, gain);
	int i = 0;
	for (; i + N <= n; i += N)
		hn::StoreU(hn::Mul(hn::LoadU(d, src + i), g), d, dst + i);
	for (; i < n; ++i) dst[i] = src[i] * gain;
}

// Interleaved (re,im) -> planar (re[] / im[]) via one Highway interleaved load.
static inline void copy_split_complex_vec(const fftw_complex* __restrict src,
	double* __restrict re,
	double* __restrict im,
	int n_complex)
{
	const double* s = reinterpret_cast<const double*>(src);
	const hn::ScalableTag<double> d;
	const int N = (int)hn::Lanes(d);
	int j = 0;
	for (; j + N <= n_complex; j += N) {
		hn::Vec<decltype(d)> vr, vi;
		hn::LoadInterleaved2(d, s + (size_t)j * 2, vr, vi);
		hn::StoreU(vr, d, re + j);
		hn::StoreU(vi, d, im + j);
	}
	// Tail
	for (; j < n_complex; ++j) {
		re[j] = s[2 * (size_t)j + 0];
		im[j] = s[2 * (size_t)j + 1];
	}
}
namespace
{
	struct PendingSingle
	{
		HConvSingle value = {};
		std::unique_ptr<HConvSingleStorage> storage = std::make_unique<HConvSingleStorage>();
		fftw::Plan fft;
		fftw::Plan ifft;

		void publish(HConvSingle* output) noexcept
		{
			value.fft = fft.release();
			value.ifft = ifft.release();
			value.storage = storage.release();
			*output = value;
		}
	};

	void validateSingleShape(int frameLength, int steps)
	{
		if (frameLength <= 0 || steps <= 0)
			throw std::invalid_argument("hcInitSingle frame length and step count must be positive");
		if (frameLength > (std::numeric_limits<int>::max)() / 2)
			throw std::length_error("hcInitSingle frame length is too large");
		if (steps == (std::numeric_limits<int>::max)())
			throw std::length_error("hcInitSingle step count is too large");
	}

	std::shared_ptr<HConvFilterBankStorage> makeFilterBank(int impulseLength, int frameLength)
	{
		const size_t frameSize = static_cast<size_t>(frameLength);
		const size_t complexLength = frameSize + 1;
		const size_t segmentCount =
			(static_cast<size_t>(impulseLength) + frameSize - 1) / frameSize;
		if (segmentCount >= static_cast<size_t>((std::numeric_limits<int>::max)()))
			throw std::length_error("hcInitSingle requires too many partitions");

		auto bank = std::make_shared<HConvFilterBankStorage>();
		bank->frameLength = frameLength;
		bank->segmentCount = static_cast<int>(segmentCount);
		bank->planeStride = (complexLength + 7) & ~static_cast<size_t>(7);
		const size_t partitionStride = checkedHcMultiply(bank->planeStride, 2);
		const size_t slabElements = checkedHcMultiply(segmentCount, partitionStride);
		bank->slab = makeHcSlab<double>(slabElements);
		bank->realPtrs.resize(segmentCount);
		bank->imagPtrs.resize(segmentCount);
		for (size_t i = 0; i < segmentCount; ++i)
		{
			bank->realPtrs[i] = bank->slab.get() + i * partitionStride;
			bank->imagPtrs[i] = bank->realPtrs[i] + bank->planeStride;
		}
		memset(bank->slab.get(), 0, checkedHcMultiply(slabElements, sizeof(double)));
		return bank;
	}

	PendingSingle prepareSingleRuntime(
		std::shared_ptr<const HConvFilterBankStorage> filterBank,
		int steps)
	{
		PendingSingle pending;
		HConvSingle& temp = pending.value;
		HConvSingleStorage& storage = *pending.storage;
		storage.filterBank = std::move(filterBank);
		const HConvFilterBankStorage& bank = *storage.filterBank;
		const size_t frameLength = static_cast<size_t>(bank.frameLength);
		const size_t dftLength = checkedHcMultiply(frameLength, 2);
		const size_t complexLength = frameLength + 1;
		const size_t mixSegmentCount = static_cast<size_t>(bank.segmentCount) + 1;
		const size_t partitionStride = checkedHcMultiply(bank.planeStride, 2);
		const size_t mixSlabElements = checkedHcMultiply(mixSegmentCount, partitionStride);

		temp.maxstep = steps;
		temp.framelength = bank.frameLength;
		temp.num_filterbuf = bank.segmentCount;
		temp.filterbuf_freq_real = bank.realPtrs.data();
		temp.filterbuf_freq_imag = bank.imagPtrs.data();

		storage.dftTime = makeHcAlignedArray<double>(dftLength);
		temp.dft_time = storage.dftTime.get();
		storage.dftFreq = makeHcAlignedArray<fftw_complex>(complexLength);
		temp.dft_freq = storage.dftFreq.get();
		storage.inFreqReal = makeHcAlignedArray<double>(complexLength);
		storage.inFreqImag = makeHcAlignedArray<double>(complexLength);
		temp.in_freq_real = storage.inFreqReal.get();
		temp.in_freq_imag = storage.inFreqImag.get();

		storage.stepTask.resize(static_cast<size_t>(steps) + 1);
		temp.steptask = storage.stepTask.data();
		int num = temp.num_filterbuf / steps;
		for (int i = 0; i <= steps; ++i)
			temp.steptask[i] = i * num;
		const int pos = temp.steptask[1] == 0 ? 1 : 2;
		num = temp.num_filterbuf % steps;
		for (int j = pos; j < pos + num; ++j)
		{
			for (int i = j; i <= steps; ++i)
				++temp.steptask[i];
		}

		temp.num_mixbuf = static_cast<int>(mixSegmentCount);
		storage.mixSlab = makeHcSlab<double>(mixSlabElements);
		storage.mixRealPtrs.resize(mixSegmentCount);
		storage.mixImagPtrs.resize(mixSegmentCount);
		temp.mixbuf_freq_real = storage.mixRealPtrs.data();
		temp.mixbuf_freq_imag = storage.mixImagPtrs.data();
		for (int i = 0; i < temp.num_mixbuf; ++i)
		{
			temp.mixbuf_freq_real[i] = storage.mixSlab.get() + static_cast<size_t>(i) * partitionStride;
			temp.mixbuf_freq_imag[i] = temp.mixbuf_freq_real[i] + bank.planeStride;
		}
		memset(storage.mixSlab.get(), 0, checkedHcMultiply(mixSlabElements, sizeof(double)));

		storage.historyTime = makeHcAlignedArray<double>(frameLength);
		temp.history_time = storage.historyTime.get();
		memset(temp.history_time, 0, checkedHcMultiply(frameLength, sizeof(double)));

		// FFTW's planner mutates process-global state. The policy session owns
		// serialization plus the import/measure/export wisdom lifecycle.
		{
			FftwPlanningPolicy::Session planning;
			const unsigned flags = planning.flags();
			pending.fft = fftw::makeRealToComplexPlan(
				2 * bank.frameLength, temp.dft_time, temp.dft_freq, flags);
			pending.ifft = fftw::makeComplexToRealPlan(
				2 * bank.frameLength, temp.dft_freq, temp.dft_time, flags);
			planning.exportWisdomForLength(2 * bank.frameLength);
		}

		return pending;
	}

	void transformFilterBank(
		PendingSingle& pending,
		const HConvFilterBankStorage& bank,
		const double* impulse,
		int impulseLength)
	{
		HConvSingle& temp = pending.value;
		const int frameLength = bank.frameLength;
		const size_t dftLength = checkedHcMultiply(static_cast<size_t>(frameLength), 2);
		const double gain = 0.5 / frameLength;
		memset(temp.dft_time, 0, checkedHcMultiply(dftLength, sizeof(double)));

		int partition = 0;
		for (; partition < bank.segmentCount - 1; ++partition)
		{
			mul_store_gain_double(
				temp.dft_time,
				impulse + static_cast<size_t>(partition) * frameLength,
				frameLength,
				gain);
			fftw_execute(pending.fft.get());
			copy_split_complex_vec(
				temp.dft_freq,
				bank.realPtrs[partition],
				bank.imagPtrs[partition],
				frameLength + 1);
		}

		const int tailLength = impulseLength - partition * frameLength;
		if (tailLength > 0)
		{
			mul_store_gain_double(temp.dft_time, impulse + static_cast<size_t>(partition) * frameLength,
				tailLength, gain);
			memset(temp.dft_time + tailLength, 0,
				checkedHcMultiply(dftLength - static_cast<size_t>(tailLength), sizeof(double)));
		}
		else
		{
			memset(temp.dft_time, 0, checkedHcMultiply(dftLength, sizeof(double)));
		}
		fftw_execute(pending.fft.get());
		copy_split_complex_vec(temp.dft_freq, bank.realPtrs[partition], bank.imagPtrs[partition],
			frameLength + 1);
	}
}

void hcInitSingle(HConvSingle* filter, const double* h, int hlen, int flen, int steps)
{
	if (filter == nullptr)
		throw std::invalid_argument("hcInitSingle requires an output filter");
	if (h == nullptr)
		throw std::invalid_argument("hcInitSingle requires impulse-response samples");
	if (hlen <= 0)
		throw std::invalid_argument("hcInitSingle impulse length must be positive");
	validateSingleShape(flen, steps);

	auto filterBank = makeFilterBank(hlen, flen);
	PendingSingle pending = prepareSingleRuntime(filterBank, steps);
	transformFilterBank(pending, *filterBank, h, hlen);
	// Publish only after every allocation, plan, and initial transform has
	// succeeded. A throwing path never exposes partial ownership to the caller.
	pending.publish(filter);
}

void hcInitSingleWithSharedFilterBank(HConvSingle* filter, const HConvSingle* prototype)
{
	if (filter == nullptr)
		throw std::invalid_argument("shared hcInitSingle requires an output filter");
	if (prototype == nullptr || prototype->storage == nullptr ||
		prototype->storage->filterBank == nullptr)
	{
		throw std::invalid_argument("shared hcInitSingle requires an initialized prototype");
	}
	if (filter == prototype)
		throw std::invalid_argument("shared hcInitSingle output must differ from its prototype");
	validateSingleShape(prototype->framelength, prototype->maxstep);

	PendingSingle pending = prepareSingleRuntime(
		prototype->storage->filterBank,
		prototype->maxstep);
	pending.publish(filter);
}

void hcCloseSingle(HConvSingle* filter)
{
	if (filter == nullptr)
		return;
	if (filter->ifft != nullptr)
		fftw_destroy_plan(filter->ifft);
	if (filter->fft != nullptr)
		fftw_destroy_plan(filter->fft);
	delete filter->storage;
	*filter = {};
}

// The dual/triple-segment (low-latency) API lives in
// libHybridConv_eapo_dormant.cpp, which no project compiles; see the
// banner there.
