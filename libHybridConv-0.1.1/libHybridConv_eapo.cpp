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
#include "../services/logging/Logging.h"
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

struct HConvPartitionedStorage
{
	std::shared_ptr<const HConvFilterBankStorage> tailFilterBank;
	HcAlignedPtr<double> dftTime;
	HcAlignedPtr<fftw_complex> dftFreq;
	fftw::Plan fft;
	fftw::Plan ifft;
	HcSlabPtr<double> inputSpectrumSlab;
	std::vector<double*> inputRealPtrs;
	std::vector<double*> inputImagPtrs;
	HcAlignedPtr<double> accumulatorReal;
	HcAlignedPtr<double> accumulatorImag;
	HcAlignedPtr<double> inputBlocks;
	HcAlignedPtr<double> outputBlocks;
	HcAlignedPtr<double> overlap;
	int blockLength = 0;
	int tailLength = 0;
	int factor = 0;
	int gatherIndex = 0;
	int gatherBlocks = 0;
	int pendingInput = -1;
	int nextPendingInput = -1;
	int activeInput = -1;
	int activeStage = 0;
	int activeOutput = -1;
	int queuedOutput = -1;
	int activeOutputBlock = 0;
	unsigned long long pendingSequence = 0;
	unsigned long long nextPendingSequence = 0;
	unsigned long long activeSequence = 0;
	unsigned long long pendingReadyCall = 0;
	unsigned long long nextPendingReadyCall = 0;
	unsigned long long processCall = 0;
	unsigned long long inputSequence = 0;
};

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

namespace
{
	constexpr int partitionedInputBufferCount = 3;
	constexpr int partitionedOutputBufferCount = 2;

	int choosePartitionedFactor(int impulseLength, int blockLength)
	{
		// Restrict K to powers of two with enough callbacks for separate FFT, MAC
		// and IFFT work. For each eligible K, estimate per-callback spectral work
		// as 2*K*B for the head plus P*B for the tail, where
		// P=ceil((N-2*K*B)/(K*B)). This deterministic bandwidth model selects K=8
		// for N=96000 at B=441/480/960 and K=16 at B=128.
		const int candidates[] = {4, 8, 16, 32};
		int bestFactor = 0;
		long long bestCost = (std::numeric_limits<long long>::max)();
		for (int factor : candidates)
		{
			const long long tailLength = static_cast<long long>(factor) * blockLength;
			if (static_cast<long long>(impulseLength) <= 2 * tailLength)
				continue;
			const long long remaining = impulseLength - 2 * tailLength;
			const long long partitionCount = (remaining + tailLength - 1) / tailLength;
			const long long cost = 2 * tailLength + partitionCount * blockLength;
			if (cost < bestCost)
			{
				bestCost = cost;
				bestFactor = factor;
			}
		}
		return bestFactor;
	}

	std::unique_ptr<HConvPartitionedStorage> preparePartitionedTail(
		std::shared_ptr<const HConvFilterBankStorage> filterBank,
		int blockLength,
		int factor)
	{
		auto storage = std::make_unique<HConvPartitionedStorage>();
		storage->tailFilterBank = std::move(filterBank);
		storage->blockLength = blockLength;
		storage->factor = factor;
		storage->tailLength = storage->tailFilterBank->frameLength;

		const size_t tailLength = static_cast<size_t>(storage->tailLength);
		const size_t dftLength = checkedHcMultiply(tailLength, 2);
		const size_t complexLength = tailLength + 1;
		const size_t partitionCount =
			static_cast<size_t>(storage->tailFilterBank->segmentCount);
		const size_t partitionStride =
			checkedHcMultiply(storage->tailFilterBank->planeStride, 2);

		storage->dftTime = makeHcAlignedArray<double>(dftLength);
		storage->dftFreq = makeHcAlignedArray<fftw_complex>(complexLength);
		storage->inputSpectrumSlab =
			makeHcSlab<double>(checkedHcMultiply(partitionCount, partitionStride));
		storage->inputRealPtrs.resize(partitionCount);
		storage->inputImagPtrs.resize(partitionCount);
		for (size_t i = 0; i < partitionCount; ++i)
		{
			storage->inputRealPtrs[i] = storage->inputSpectrumSlab.get() + i * partitionStride;
			storage->inputImagPtrs[i] =
				storage->inputRealPtrs[i] + storage->tailFilterBank->planeStride;
		}
		storage->accumulatorReal = makeHcAlignedArray<double>(complexLength);
		storage->accumulatorImag = makeHcAlignedArray<double>(complexLength);
		storage->inputBlocks = makeHcAlignedArray<double>(
			checkedHcMultiply(tailLength, partitionedInputBufferCount));
		storage->outputBlocks = makeHcAlignedArray<double>(
			checkedHcMultiply(tailLength, partitionedOutputBufferCount));
		storage->overlap = makeHcAlignedArray<double>(tailLength);

		{
			FftwPlanningPolicy::Session planning;
			const unsigned flags = planning.flags();
			storage->fft = fftw::makeRealToComplexPlan(
				2 * storage->tailLength, storage->dftTime.get(), storage->dftFreq.get(), flags);
			storage->ifft = fftw::makeComplexToRealPlan(
				2 * storage->tailLength, storage->dftFreq.get(), storage->dftTime.get(), flags);
			planning.exportWisdomForLength(2 * storage->tailLength);
		}

		// FFTW_MEASURE may overwrite its plan buffers. Initialize every runtime
		// buffer only after both plans exist so the first callback sees zeros.
		memset(storage->dftTime.get(), 0, checkedHcMultiply(dftLength, sizeof(double)));
		memset(storage->dftFreq.get(), 0, checkedHcMultiply(complexLength, sizeof(fftw_complex)));
		memset(storage->inputSpectrumSlab.get(), 0,
			checkedHcMultiply(checkedHcMultiply(partitionCount, partitionStride), sizeof(double)));
		memset(storage->accumulatorReal.get(), 0,
			checkedHcMultiply(complexLength, sizeof(double)));
		memset(storage->accumulatorImag.get(), 0,
			checkedHcMultiply(complexLength, sizeof(double)));
		memset(storage->inputBlocks.get(), 0,
			checkedHcMultiply(checkedHcMultiply(tailLength, partitionedInputBufferCount), sizeof(double)));
		memset(storage->outputBlocks.get(), 0,
			checkedHcMultiply(checkedHcMultiply(tailLength, partitionedOutputBufferCount), sizeof(double)));
		memset(storage->overlap.get(), 0, checkedHcMultiply(tailLength, sizeof(double)));
		return storage;
	}

	void transformPartitionedTail(
		HConvPartitionedStorage& storage,
		const HConvFilterBankStorage& bank,
		const double* impulse,
		int impulseLength)
	{
		const int tailLength = storage.tailLength;
		const size_t dftLength = checkedHcMultiply(static_cast<size_t>(tailLength), 2);
		const double gain = 0.5 / tailLength;
		for (int partition = 0; partition < bank.segmentCount; ++partition)
		{
			const int offset = partition * tailLength;
			const int copyLength = (std::min)(tailLength, impulseLength - offset);
			mul_store_gain_double(storage.dftTime.get(), impulse + offset, copyLength, gain);
			memset(storage.dftTime.get() + copyLength, 0,
				checkedHcMultiply(dftLength - static_cast<size_t>(copyLength), sizeof(double)));
			fftw_execute(storage.fft.get());
			copy_split_complex_vec(storage.dftFreq.get(), bank.realPtrs[partition],
				bank.imagPtrs[partition], tailLength + 1);
		}
	}

	void macPartitionedTail(HConvPartitionedStorage& storage, int start, int stop)
	{
		const HConvFilterBankStorage& bank = *storage.tailFilterBank;
		const size_t elementCount = static_cast<size_t>(storage.tailLength) + 1;
		const int partitionCount = bank.segmentCount;
		const int validPartitions = static_cast<int>((std::min)(
			storage.activeSequence + 1,
			static_cast<unsigned long long>(partitionCount)));
		stop = (std::min)(stop, validPartitions);

		const hn::ScalableTag<double> d;
		const size_t laneCount = hn::Lanes(d);
		for (int partition = start; partition < stop; ++partition)
		{
			const unsigned long long inputSequence = storage.activeSequence - partition;
			const int inputSlot = static_cast<int>(
				inputSequence % static_cast<unsigned long long>(partitionCount));
			const double* xReal = storage.inputRealPtrs[inputSlot];
			const double* xImag = storage.inputImagPtrs[inputSlot];
			const double* hReal = bank.realPtrs[partition];
			const double* hImag = bank.imagPtrs[partition];
			double* yReal = storage.accumulatorReal.get();
			double* yImag = storage.accumulatorImag.get();
			size_t n = 0;
			for (; n + laneCount <= elementCount; n += laneCount)
			{
				const auto xr = hn::LoadU(d, xReal + n);
				const auto xi = hn::LoadU(d, xImag + n);
				const auto hr = hn::LoadU(d, hReal + n);
				const auto hi = hn::LoadU(d, hImag + n);
				auto yr = hn::LoadU(d, yReal + n);
				auto yi = hn::LoadU(d, yImag + n);
				yr = hn::MulAdd(xr, hr, yr);
				yr = hn::NegMulAdd(xi, hi, yr);
				yi = hn::MulAdd(xr, hi, yi);
				yi = hn::MulAdd(xi, hr, yi);
				hn::StoreU(yr, d, yReal + n);
				hn::StoreU(yi, d, yImag + n);
			}
			for (; n < elementCount; ++n)
			{
				yReal[n] += xReal[n] * hReal[n] - xImag[n] * hImag[n];
				yImag[n] += xReal[n] * hImag[n] + xImag[n] * hReal[n];
			}
		}
	}

	int findFreePartitionedInput(const HConvPartitionedStorage& storage)
	{
		for (int index = 0; index < partitionedInputBufferCount; ++index)
		{
			if (index != storage.activeInput && index != storage.pendingInput &&
				index != storage.nextPendingInput)
				return index;
		}
		return -1;
	}

	void beginPartitionedTailJob(HConvPartitionedStorage& storage)
	{
		storage.activeInput = storage.pendingInput;
		storage.activeSequence = storage.pendingSequence;
		storage.activeStage = 0;
		storage.pendingInput = storage.nextPendingInput;
		storage.pendingSequence = storage.nextPendingSequence;
		storage.pendingReadyCall = storage.nextPendingReadyCall;
		storage.nextPendingInput = -1;
	}

	void transformPartitionedInput(HConvPartitionedStorage& storage)
	{
		const size_t tailLength = static_cast<size_t>(storage.tailLength);
		const size_t complexLength = tailLength + 1;
		const double* input = storage.inputBlocks.get() +
			static_cast<size_t>(storage.activeInput) * tailLength;
		memcpy(storage.dftTime.get(), input, checkedHcMultiply(tailLength, sizeof(double)));
		memset(storage.dftTime.get() + tailLength, 0,
			checkedHcMultiply(tailLength, sizeof(double)));
		fftw_execute(storage.fft.get());
		const int inputSlot = static_cast<int>(storage.activeSequence %
			static_cast<unsigned long long>(storage.tailFilterBank->segmentCount));
		copy_split_complex_vec(storage.dftFreq.get(), storage.inputRealPtrs[inputSlot],
			storage.inputImagPtrs[inputSlot], static_cast<int>(complexLength));
		memset(storage.accumulatorReal.get(), 0,
			checkedHcMultiply(complexLength, sizeof(double)));
		memset(storage.accumulatorImag.get(), 0,
			checkedHcMultiply(complexLength, sizeof(double)));
	}

	void finishPartitionedTailJob(HConvPartitionedStorage& storage)
	{
		const int complexLength = storage.tailLength + 1;
		for (int i = 0; i < complexLength; ++i)
		{
			storage.dftFreq.get()[i][0] = storage.accumulatorReal.get()[i];
			storage.dftFreq.get()[i][1] = storage.accumulatorImag.get()[i];
		}
		fftw_execute(storage.ifft.get());

		int outputIndex = 0;
		if (storage.activeOutput == 0 || storage.queuedOutput == 0)
			outputIndex = 1;
		double* output = storage.outputBlocks.get() +
			static_cast<size_t>(outputIndex) * storage.tailLength;
		add_out_hist_to_y_simd(storage.dftTime.get(), storage.overlap.get(), output,
			storage.tailLength, 0);
		copy_hist_from_out_tail_simd(storage.overlap.get(),
			storage.dftTime.get() + storage.tailLength, storage.tailLength);
		storage.queuedOutput = outputIndex;
		storage.activeInput = -1;
	}
}

void hcInitPartitioned(HConvPartitioned* filter, const double* h, int hlen, int flen)
{
	if (filter == nullptr)
		throw std::invalid_argument("hcInitPartitioned requires an output filter");
	if (h == nullptr)
		throw std::invalid_argument("hcInitPartitioned requires impulse-response samples");
	if (hlen <= 0)
		throw std::invalid_argument("hcInitPartitioned impulse length must be positive");
	validateSingleShape(flen, 1);

	HConvPartitioned pending = {};
	pending.framelength = flen;
	const int factor = choosePartitionedFactor(hlen, flen);
	try
	{
		if (factor == 0)
		{
			hcInitSingle(&pending.head, h, hlen, flen, 1);
			*filter = pending;
			return;
		}

		const long long tailLength64 = static_cast<long long>(factor) * flen;
		if (tailLength64 > (std::numeric_limits<int>::max)() / 2)
			throw std::length_error("hcInitPartitioned tail partition is too large");
		const int tailLength = static_cast<int>(tailLength64);
		const int headLength = 2 * tailLength;
		const int tailImpulseLength = hlen - headLength;

		// The cost model above keeps K deterministic for a given N and B. At
		// B=480, N=96000 it selects K=8: a 7680-sample head (16 B-sized
		// partitions) and a 23-partition tail with a 7680-point FFT, reducing
		// average filter-spectrum traffic to about one fifth of the uniform path.
		pending.tailfactor = factor;
		hcInitSingle(&pending.head, h, headLength, flen, 1);
		auto tailBank = makeFilterBank(tailImpulseLength, tailLength);
		auto storage = preparePartitionedTail(tailBank, flen, factor);
		transformPartitionedTail(*storage, *tailBank, h + headLength, tailImpulseLength);
		pending.storage = storage.release();
		*filter = pending;
	}
	catch (...)
	{
		hcCloseSingle(&pending.head);
		delete pending.storage;
		throw;
	}
}

void hcInitPartitionedWithSharedFilterBank(HConvPartitioned* filter,
	const HConvPartitioned* prototype)
{
	if (filter == nullptr)
		throw std::invalid_argument("shared hcInitPartitioned requires an output filter");
	if (prototype == nullptr || prototype->head.storage == nullptr)
		throw std::invalid_argument("shared hcInitPartitioned requires an initialized prototype");
	if (filter == prototype)
		throw std::invalid_argument("shared hcInitPartitioned output must differ from its prototype");

	HConvPartitioned pending = {};
	pending.framelength = prototype->framelength;
	pending.tailfactor = prototype->tailfactor;
	try
	{
		hcInitSingleWithSharedFilterBank(&pending.head, &prototype->head);
		if (prototype->storage != nullptr)
		{
			auto storage = preparePartitionedTail(
				prototype->storage->tailFilterBank,
				prototype->storage->blockLength,
				prototype->storage->factor);
			pending.storage = storage.release();
		}
		*filter = pending;
	}
	catch (...)
	{
		hcCloseSingle(&pending.head);
		delete pending.storage;
		throw;
	}
}

void hcPutPartitioned(HConvPartitioned* filter, double* x)
{
	hcPutSingle(&filter->head, x);
	HConvPartitionedStorage* storage = filter->storage;
	if (storage == nullptr)
		return;

	const size_t blockLength = static_cast<size_t>(storage->blockLength);
	const size_t tailLength = static_cast<size_t>(storage->tailLength);
	double* gather = storage->inputBlocks.get() +
		static_cast<size_t>(storage->gatherIndex) * tailLength;
	memcpy(gather + static_cast<size_t>(storage->gatherBlocks) * blockLength,
		x, checkedHcMultiply(blockLength, sizeof(double)));
	++storage->gatherBlocks;
	if (storage->gatherBlocks != storage->factor)
		return;

	const int completedInput = storage->gatherIndex;
	const unsigned long long sequence = storage->inputSequence++;
	const unsigned long long readyCall = storage->processCall + 1;
	if (storage->pendingInput < 0)
	{
		storage->pendingInput = completedInput;
		storage->pendingSequence = sequence;
		storage->pendingReadyCall = readyCall;
	}
	else
	{
		storage->nextPendingInput = completedInput;
		storage->nextPendingSequence = sequence;
		storage->nextPendingReadyCall = readyCall;
	}
	storage->gatherBlocks = 0;
	storage->gatherIndex = findFreePartitionedInput(*storage);
}

void hcProcessPartitioned(HConvPartitioned* filter)
{
	hcProcessSingle(&filter->head);
	HConvPartitionedStorage* storage = filter->storage;
	if (storage == nullptr)
		return;

	if (storage->activeInput < 0 && storage->pendingInput >= 0 &&
		storage->pendingReadyCall <= storage->processCall)
		beginPartitionedTailJob(*storage);

	if (storage->activeInput >= 0)
	{
		const int stage = storage->activeStage;
		const int factor = storage->factor;
		const int partitionCount = storage->tailFilterBank->segmentCount;
		if (stage == 0)
		{
			transformPartitionedInput(*storage);
			if (factor < 3)
				macPartitionedTail(*storage, 0, partitionCount);
		}
		else if (stage < factor - 1)
		{
			const int macStageCount = factor - 2;
			const int macStage = stage - 1;
			const int start = macStage * partitionCount / macStageCount;
			const int stop = (macStage + 1) * partitionCount / macStageCount;
			macPartitionedTail(*storage, start, stop);
		}

		if (stage == factor - 1)
			finishPartitionedTailJob(*storage);
		else
			++storage->activeStage;
	}

	++storage->processCall;
	if (storage->gatherIndex < 0)
		storage->gatherIndex = findFreePartitionedInput(*storage);
}

void hcGetPartitioned(HConvPartitioned* filter, double* y)
{
	hcGetSingle(&filter->head, y);
	HConvPartitionedStorage* storage = filter->storage;
	if (storage == nullptr)
		return;

	if (storage->activeOutput >= 0)
	{
		const size_t offset = static_cast<size_t>(storage->activeOutput) * storage->tailLength +
			static_cast<size_t>(storage->activeOutputBlock) * storage->blockLength;
		const double* tail = storage->outputBlocks.get() + offset;
		const hn::ScalableTag<double> d;
		const size_t laneCount = hn::Lanes(d);
		size_t i = 0;
		for (; i + laneCount <= static_cast<size_t>(storage->blockLength); i += laneCount)
			hn::StoreU(hn::Add(hn::LoadU(d, y + i), hn::LoadU(d, tail + i)), d, y + i);
		for (; i < static_cast<size_t>(storage->blockLength); ++i)
			y[i] += tail[i];
		++storage->activeOutputBlock;
		if (storage->activeOutputBlock == storage->factor)
		{
			storage->activeOutput = -1;
			storage->activeOutputBlock = 0;
		}
	}
	if (storage->activeOutput < 0 && storage->queuedOutput >= 0)
	{
		storage->activeOutput = storage->queuedOutput;
		storage->queuedOutput = -1;
	}
}

void hcClosePartitioned(HConvPartitioned* filter)
{
	if (filter == nullptr)
		return;
	hcCloseSingle(&filter->head);
	delete filter->storage;
	*filter = {};
}

// The dual/triple-segment (low-latency) API and its dormant carrier files
// were removed in audit #275 (TD-15): no project compiled them.
