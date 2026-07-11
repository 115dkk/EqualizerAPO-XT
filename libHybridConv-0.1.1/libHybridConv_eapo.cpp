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
 // Adapted version for Equalizer APO. For original version see libHybridConv.c

#include "stdafx.h"
#ifndef _M_ARM64
#include <immintrin.h>   // only for _mm_prefetch; vector math goes through Highway
#endif
#include <algorithm>
#include <memory>
#include <mutex>
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
#include "../helpers/LogHelper.h"
#include "HcAlignedStorage.h"
#include "libHybridConv_eapo.h"

// stdafx.h includes <windows.h> without NOMINMAX, so min/max are defined as
// macros here. Undefine them before Highway, whose templates use std::min and
// std::max.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

// Definitions of the instance-owned storage declared in libHybridConv_eapo.h.
// hcInit* allocates one per instance and the matching hcClose* deletes it, so
// no state is shared between instances; the FFTW planner lock in hcInitSingle
// stays the only cross-instance synchronization in this file.
struct HConvSingleStorage
{
	HcAlignedPtr<double> dftTime;
	HcAlignedPtr<fftw_complex> dftFreq;
	HcAlignedPtr<double> inFreqReal;
	HcAlignedPtr<double> inFreqImag;
	std::vector<int> stepTask;
	// The per-partition real/imag planes live in two contiguous slabs (filter
	// read-only, mix read-modify-write) so the per-block partition sweep walks
	// two allocations instead of hundreds of scattered heap blocks. The plane
	// layout inside the slabs is pinned by HybridConvTests.
	HcSlabPtr<double> filterSlab;
	HcSlabPtr<double> mixSlab;
	std::vector<double*> filterRealPtrs;
	std::vector<double*> filterImagPtrs;
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

double getProcTime(int flen, int num, double dur)
{
	HConvSingle filter;
	int xlen, hlen, ylen;
	int n;
	int pos;
	double t_start, t_diff;
	double counter = 0.0;
	double proc_time;
	double lin, mul;

	xlen = 2048 * 2048;
	std::vector<double> x(xlen);
	lin = pow(10.0, -100.0 / 20.0);	// 0.00001 = -100dB
	mul = pow(lin, 1.0 / static_cast<double>(xlen));
	x[0] = 1.0;
	for (n = 1; n < xlen; n++)
		x[n] = -mul * x[n - 1];

	hlen = flen * num;
	std::vector<double> h(hlen);
	lin = pow(10.0, -60.0 / 20.0);	// 0.001 = -60dB
	mul = pow(lin, 1.0 / static_cast<double>(hlen));
	h[0] = 1.0;
	for (n = 1; n < hlen; n++)
		h[n] = mul * h[n - 1];

	ylen = flen;
	std::vector<double> y(ylen);

	hcInitSingle(&filter, h.data(), hlen, flen, 1);

	t_diff = 0.0;
	t_start = hcTime();
	pos = 0;
	while (t_diff < dur)
	{
		hcPutSingle(&filter, &x[pos]);
		hcProcessSingle(&filter);
		hcGetSingle(&filter, y.data());
		pos += flen;
		if (pos >= xlen)
			pos = 0;
		counter += 1.0;
		t_diff = hcTime() - t_start;
	}
	proc_time = t_diff / counter;
	LogFStatic(L"Processing time: %7.3f us", 1000000.0 * proc_time);

	hcCloseSingle(&filter);

	return proc_time;
}

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
	const double* dft_freq_d = (const double*)dft_freq;

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
			_mm_prefetch((char const*)(filter->filterbuf_freq_real[s + prefetch_distance]), _MM_HINT_T0);
			_mm_prefetch((char const*)(filter->filterbuf_freq_imag[s + prefetch_distance]), _MM_HINT_T0);
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

	double* out = filter->dft_time;        // length = 2*flen
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

	double* out = filter->dft_time;        // length = 2*flen
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

static inline void copy_split_complex_scalar(const fftw_complex * __restrict src,
	double* __restrict re,
	double* __restrict im,
	int n_complex)
{
	// n_complex = flen + 1
	for (int j = 0; j < n_complex; ++j) {
		re[j] = src[j][0];
		im[j] = src[j][1];
	}
}

// Interleaved (re,im) -> planar (re[] / im[]) via one Highway interleaved load.
static inline void copy_split_complex_vec(const fftw_complex* __restrict src,
	double* __restrict re,
	double* __restrict im,
	int n_complex)
{
	const double* s = (const double*)src;
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
void hcInitSingle(HConvSingle * filter, double* h, int hlen, int flen, int steps)
{
	int i, j, size, num, pos;
	double gain;
	// The struct may come from uninitialized stack or heap memory, so the old
	// storage pointer must not be read here. Every init pairs with a
	// hcCloseSingle, which frees it.
	filter->storage = new HConvSingleStorage();
	auto& storage = *filter->storage;

	filter->step = 0;
	filter->maxstep = steps;
	filter->mixpos = 0;
	filter->framelength = flen;

	size = sizeof *filter->dft_time * 2 * flen;
	storage.dftTime = makeHcAlignedArray<double>(2 * (size_t)flen);
	filter->dft_time = storage.dftTime.get();

	size = sizeof(fftw_complex) * (flen + 1);
	storage.dftFreq = makeHcAlignedArray<fftw_complex>((size_t)flen + 1);
	filter->dft_freq = storage.dftFreq.get();

	size = sizeof *filter->in_freq_real * (flen + 1);
	storage.inFreqReal = makeHcAlignedArray<double>((size_t)flen + 1);
	storage.inFreqImag = makeHcAlignedArray<double>((size_t)flen + 1);
	filter->in_freq_real = storage.inFreqReal.get();
	filter->in_freq_imag = storage.inFreqImag.get();

	filter->num_filterbuf = (hlen + flen - 1) / flen;

	size = sizeof *filter->steptask * (steps + 1);
	storage.stepTask.resize((size_t)steps + 1);
	filter->steptask = storage.stepTask.data();
	num = filter->num_filterbuf / steps;
	for (i = 0; i <= steps; i++)
		filter->steptask[i] = i * num;
	pos = (filter->steptask[1] == 0) ? 1 : 2;
	num = filter->num_filterbuf % steps;
	for (j = pos; j < pos + num; j++) {
		for (i = j; i <= steps; i++)
			filter->steptask[i]++;
	}

	// Each plane is padded to a whole number of cache lines so every real and
	// imag plane starts 64-byte aligned; a partition is [real|imag]. The
	// memset doubles as a pre-touch so first use on the audio thread does not
	// take the soft page faults.
	const size_t planeStride = ((size_t)flen + 1 + 7) & ~(size_t)7;
	const size_t partitionStride = 2 * planeStride;

	storage.filterSlab = makeHcSlab<double>((size_t)filter->num_filterbuf * partitionStride);
	storage.filterRealPtrs.resize(filter->num_filterbuf);
	storage.filterImagPtrs.resize(filter->num_filterbuf);
	filter->filterbuf_freq_real = storage.filterRealPtrs.data();
	filter->filterbuf_freq_imag = storage.filterImagPtrs.data();
	for (i = 0; i < filter->num_filterbuf; i++) {
		filter->filterbuf_freq_real[i] = storage.filterSlab.get() + (size_t)i * partitionStride;
		filter->filterbuf_freq_imag[i] = filter->filterbuf_freq_real[i] + planeStride;
	}
	memset(storage.filterSlab.get(), 0, (size_t)filter->num_filterbuf * partitionStride * sizeof(double));

	filter->num_mixbuf = filter->num_filterbuf + 1;

	storage.mixSlab = makeHcSlab<double>((size_t)filter->num_mixbuf * partitionStride);
	storage.mixRealPtrs.resize(filter->num_mixbuf);
	storage.mixImagPtrs.resize(filter->num_mixbuf);
	filter->mixbuf_freq_real = storage.mixRealPtrs.data();
	filter->mixbuf_freq_imag = storage.mixImagPtrs.data();
	for (i = 0; i < filter->num_mixbuf; i++) {
		filter->mixbuf_freq_real[i] = storage.mixSlab.get() + (size_t)i * partitionStride;
		filter->mixbuf_freq_imag[i] = filter->mixbuf_freq_real[i] + planeStride;
	}
	memset(storage.mixSlab.get(), 0, (size_t)filter->num_mixbuf * partitionStride * sizeof(double));

	size = sizeof *filter->history_time * flen;
	storage.historyTime = makeHcAlignedArray<double>(flen);
	filter->history_time = storage.historyTime.get();
	memset(filter->history_time, 0, size);

	// When a cached wisdom file exists we use FFTW_MEASURE — planner returns immediately from the cache,
	// giving the optimized plan without the multi-second learning cost. Without wisdom we fall back to
	// FFTW_ESTIMATE so a fresh install does not introduce a multi-second audio glitch at startup.
	// A separate warmup tool can pre-populate %LOCALAPPDATA%\EqualizerAPO\fftw_wisdom.dat to unlock the
	// faster MEASURE path on subsequent runs.
	{
		static std::mutex plannerMutex;
		std::lock_guard<std::mutex> lock(plannerMutex);
		char appData[MAX_PATH];
		static std::string wisdomPath;
		static bool wisdomAvailable = false;
		if (wisdomPath.empty() && GetEnvironmentVariableA("LOCALAPPDATA", appData, MAX_PATH) > 0)
		{
			std::string dir = std::string(appData) + "\\EqualizerAPO";
			CreateDirectoryA(dir.c_str(), nullptr);
			wisdomPath = dir + "\\fftw_wisdom.dat";
			static std::once_flag importedFlag;
			std::call_once(importedFlag, []() {
				if (!wisdomPath.empty() && GetFileAttributesA(wisdomPath.c_str()) != INVALID_FILE_ATTRIBUTES)
					wisdomAvailable = (fftw_import_wisdom_from_filename(wisdomPath.c_str()) != 0);
			});
		}
		unsigned fftw_flags = (wisdomAvailable ? FFTW_MEASURE : FFTW_ESTIMATE) | FFTW_PRESERVE_INPUT;
		filter->fft = fftw_plan_dft_r2c_1d(2 * flen, filter->dft_time, filter->dft_freq, fftw_flags);
		filter->ifft = fftw_plan_dft_c2r_1d(2 * flen, filter->dft_freq, filter->dft_time, fftw_flags);
	}

	gain = 0.5 / flen;

	memset(filter->dft_time, 0, sizeof *filter->dft_time * 2 * flen);

	// Full-length segments
	for (i = 0; i < filter->num_filterbuf - 1; i++) {
		// dft_time[0:flen] = gain * h[i * flen + 0 : + flen]
		mul_store_gain_double(filter->dft_time, h + (size_t)i * flen, flen, gain);

		fftw_execute(filter->fft);

		// Split complex to separate real/imag buffers
		copy_split_complex_vec((const fftw_complex*)filter->dft_freq,
			filter->filterbuf_freq_real[i],
			filter->filterbuf_freq_imag[i],
			flen + 1);
	}

	// Tail (possibly partial) segment
	int last_segment_len = hlen - i * flen;
	if (last_segment_len > 0) {
		mul_store_gain_double(filter->dft_time, h + (size_t)i * flen, last_segment_len, gain);
		// zero the remainder up to 2*flen
		memset(&filter->dft_time[last_segment_len], 0,
			sizeof *filter->dft_time * (2 * (size_t)flen - (size_t)last_segment_len));
	}
	else {
		// No tail data: ensure the time buffer is zeroed
		memset(filter->dft_time, 0, sizeof *filter->dft_time * 2 * flen);
	}

	fftw_execute(filter->fft);
	copy_split_complex_vec((const fftw_complex*)filter->dft_freq,
		filter->filterbuf_freq_real[i],
		filter->filterbuf_freq_imag[i],
		flen + 1);
}

void hcCloseSingle(HConvSingle* filter)
{
	fftw_destroy_plan(filter->ifft);
	fftw_destroy_plan(filter->fft);
	delete filter->storage;
	memset(filter, 0, sizeof(HConvSingle));
}

// The dual/triple-segment (low-latency) API lives in
// libHybridConv_eapo_dormant.cpp, which no project compiles; see the
// banner there.
