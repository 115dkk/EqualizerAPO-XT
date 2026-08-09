

// DORMANT CODE - deliberately compiled by NO project.
//
// The dual/triple-segment (low-latency) convolver API was ported through the
// double-precision rewrite but has no callers: every filter in the engine
// uses the Single API. It is kept - explicitly parked, not forgotten debt -
// because a low-latency mode for heavy convolution stacks (and a possible
// multicore split) is planned future work. To revive: add this .cpp to Common.vcxproj and
// Editor/Editor.pro next to libHybridConv_eapo.cpp, include the _dormant.h
// where needed, and rerun hcBenchmarkDual/Tripple before first use.

#include <string.h>
#include <vector>
#include <memory>
#include <math.h>
#include <fftw3.h>
#include "../services/logging/LogHelper.h"
#include "HcAlignedStorage.h"
#include "libHybridConv_eapo_dormant.h"

struct HConvDualStorage
{
	HcAlignedPtr<double> paddedFilter;
	HcAlignedPtr<double> inLong;
	HcAlignedPtr<double> outLong;
	std::unique_ptr<HConvSingle> shortFilter;
	std::unique_ptr<HConvSingle> longFilter;
};

struct HConvTrippleStorage
{
	HcAlignedPtr<double> paddedFilter;
	HcAlignedPtr<double> inMedium;
	HcAlignedPtr<double> outMedium;
	std::unique_ptr<HConvSingle> shortFilter;
	std::unique_ptr<HConvDual> mediumFilter;
};


void hcBenchmarkDual(int sflen, int lflen)
{
	HConvDual filter;
	int xlen, hlen, ylen, n, pos;
	double t_start, t_diff, counter = 0.0, signal_time, cpu_load, lin, mul;

	xlen = 2048 * 2048;
	std::vector<double> x(xlen);
	lin = pow(10.0, -100.0 / 20.0);
	mul = pow(lin, 1.0 / static_cast<double>(xlen));
	x[0] = 1.0;
	for (n = 1; n < xlen; n++)
		x[n] = -mul * x[n - 1];

	hlen = 96000;
	std::vector<double> h(hlen);
	lin = pow(10.0, -60.0 / 20.0);
	mul = pow(lin, 1.0 / static_cast<double>(hlen));
	h[0] = 1.0;
	for (n = 1; n < hlen; n++)
		h[n] = mul * h[n - 1];

	ylen = sflen;
	std::vector<double> y(ylen);

	hcInitDual(&filter, h.data(), hlen, sflen, lflen);

	t_diff = 0.0;
	t_start = hcTime();
	pos = 0;
	while (t_diff < 10.0) {
		hcProcessDual(&filter, &(x[pos]), y.data());
		pos += sflen;
		if (pos >= xlen) pos = 0;
		counter += 1.0;
		t_diff = hcTime() - t_start;
	}
	signal_time = counter * sflen / 48000.0;
	cpu_load = 100.0 * t_diff / signal_time;
	LogFStatic(L"Estimated CPU load: %5.2f %%", cpu_load);

	hcCloseDual(&filter);
}


void hcProcessDual(HConvDual* filter, double* in, double* out)
{
	// This function calls the optimized single-filter functions
	hcPutSingle(filter->f_short, in);
	hcProcessSingle(filter->f_short);
	hcGetSingle(filter->f_short, out);

	const int lpos = filter->step * filter->flen_short;
	for (int i = 0; i < filter->flen_short; i++)
		out[i] += filter->out_long[lpos + i];

	if (filter->step == 0)
		hcPutSingle(filter->f_long, filter->in_long);
	hcProcessSingle(filter->f_long);
	if (filter->step == filter->maxstep - 1)
		hcGetSingle(filter->f_long, filter->out_long);

	memcpy(&(filter->in_long[lpos]), in, sizeof *filter->in_long * filter->flen_short);
	filter->step = (filter->step + 1) % filter->maxstep;
}


void hcProcessAddDual(HConvDual* filter, double* in, double* out)
{
	hcPutSingle(filter->f_short, in);
	hcProcessSingle(filter->f_short);
	hcGetAddSingle(filter->f_short, out);

	const int lpos = filter->step * filter->flen_short;
	for (int i = 0; i < filter->flen_short; i++)
		out[i] += filter->out_long[lpos + i];

	if (filter->step == 0)
		hcPutSingle(filter->f_long, filter->in_long);
	hcProcessSingle(filter->f_long);
	if (filter->step == filter->maxstep - 1)
		hcGetSingle(filter->f_long, filter->out_long);

	memcpy(&(filter->in_long[lpos]), in, sizeof *filter->in_long * filter->flen_short);
	filter->step = (filter->step + 1) % filter->maxstep;
}


void hcInitDual(HConvDual* filter, double* h, int hlen, int sflen, int lflen)
{
	int size;
	double* h2 = nullptr;
	int h2len = 2 * lflen + 1;
	filter->storage = new HConvDualStorage();
	auto& storage = *filter->storage;

	if (hlen < h2len) {
		size = sizeof *h2 * h2len;
		storage.paddedFilter = makeHcAlignedArray<double>(h2len);
		h2 = storage.paddedFilter.get();
		memset(h2, 0, size);
		memcpy(h2, h, sizeof *h2 * hlen);
		h = h2;
		hlen = h2len;
	}

	filter->step = 0;
	filter->maxstep = lflen / sflen;
	filter->flen_long = lflen;
	filter->flen_short = sflen;

	size = sizeof *filter->in_long * lflen;
	storage.inLong = makeHcAlignedArray<double>(lflen);
	filter->in_long = storage.inLong.get();
	memset(filter->in_long, 0, size);
	storage.outLong = makeHcAlignedArray<double>(lflen);
	filter->out_long = storage.outLong.get();
	memset(filter->out_long, 0, size);

	storage.shortFilter = std::make_unique<HConvSingle>();
	filter->f_short = storage.shortFilter.get();
	hcInitSingle(filter->f_short, h, 2 * lflen, sflen, 1);

	storage.longFilter = std::make_unique<HConvSingle>();
	filter->f_long = storage.longFilter.get();
	hcInitSingle(filter->f_long, &(h[2 * lflen]), hlen - 2 * lflen, lflen, lflen / sflen);
}


void hcCloseDual(HConvDual* filter)
{
	hcCloseSingle(filter->f_short);
	hcCloseSingle(filter->f_long);
	delete filter->storage;
	memset(filter, 0, sizeof(HConvDual));
}


////////////////////////////////////////////////////////////////


void hcBenchmarkTripple(int sflen, int mflen, int lflen)
{
	HConvTripple filter;
	int xlen, hlen, ylen, size, n, pos;
	double t_start, t_diff, counter = 0.0, signal_time, cpu_load, lin, mul;

	xlen = 2048 * 2048;
	std::vector<double> x(xlen);
	lin = pow(10.0, -100.0 / 20.0);
	mul = pow(lin, 1.0 / static_cast<double>(xlen));
	x[0] = 1.0;
	for (n = 1; n < xlen; n++)
		x[n] = -mul * x[n - 1];

	hlen = 96000;
	std::vector<double> h(hlen);
	lin = pow(10.0, -60.0 / 20.0);
	mul = pow(lin, 1.0 / static_cast<double>(hlen));
	h[0] = 1.0;
	for (n = 1; n < hlen; n++)
		h[n] = mul * h[n - 1];

	ylen = sflen;
	std::vector<double> y(ylen);

	hcInitTripple(&filter, h.data(), hlen, sflen, mflen, lflen);

	t_diff = 0.0;
	t_start = hcTime();
	size = mflen / sflen;
	pos = 0;
	while (t_diff < 10.0) {
		for (n = 0; n < size; n++) {
			hcProcessTripple(&filter, &(x[pos]), y.data());
			pos += sflen;
			if (pos >= xlen) pos = 0;
		}
		counter += 1.0;
		t_diff = hcTime() - t_start;
	}
	signal_time = counter * static_cast<double>(mflen) / 48000.0;
	cpu_load = 100.0 * t_diff / signal_time;
	LogFStatic(L"Estimated CPU load: %5.2f %%", cpu_load);

	hcCloseTripple(&filter);
}


void hcProcessTripple(HConvTripple* filter, double* in, double* out)
{
	hcPutSingle(filter->f_short, in);
	hcProcessSingle(filter->f_short);
	hcGetSingle(filter->f_short, out);

	const int lpos = filter->step * filter->flen_short;
	for (int i = 0; i < filter->flen_short; i++)
		out[i] += filter->out_medium[lpos + i];

	memcpy(&(filter->in_medium[lpos]), in, sizeof *filter->in_medium * filter->flen_short);

	if (filter->step == filter->maxstep - 1)
		hcProcessDual(filter->f_medium, filter->in_medium, filter->out_medium);

	filter->step = (filter->step + 1) % filter->maxstep;
}


void hcProcessAddTripple(HConvTripple* filter, double* in, double* out)
{
	hcPutSingle(filter->f_short, in);
	hcProcessSingle(filter->f_short);
	hcGetAddSingle(filter->f_short, out);

	const int lpos = filter->step * filter->flen_short;
	for (int i = 0; i < filter->flen_short; i++)
		out[i] += filter->out_medium[lpos + i];

	memcpy(&(filter->in_medium[lpos]), in, sizeof *filter->in_medium * filter->flen_short);

	if (filter->step == filter->maxstep - 1)
		hcProcessDual(filter->f_medium, filter->in_medium, filter->out_medium);

	filter->step = (filter->step + 1) % filter->maxstep;
}


void hcInitTripple(HConvTripple* filter, double* h, int hlen, int sflen, int mflen, int lflen)
{
	int size;
	double* h2 = nullptr;
	int h2len = mflen + 2 * lflen + 1;
	filter->storage = new HConvTrippleStorage();
	auto& storage = *filter->storage;

	if (hlen < h2len) {
		size = sizeof *h2 * h2len;
		storage.paddedFilter = makeHcAlignedArray<double>(h2len);
		h2 = storage.paddedFilter.get();
		memset(h2, 0, size);
		memcpy(h2, h, sizeof *h2 * hlen);
		h = h2;
		hlen = h2len;
	}

	filter->step = 0;
	filter->maxstep = mflen / sflen;
	filter->flen_medium = mflen;
	filter->flen_short = sflen;

	size = sizeof *filter->in_medium * mflen;
	storage.inMedium = makeHcAlignedArray<double>(mflen);
	filter->in_medium = storage.inMedium.get();
	memset(filter->in_medium, 0, size);
	storage.outMedium = makeHcAlignedArray<double>(mflen);
	filter->out_medium = storage.outMedium.get();
	memset(filter->out_medium, 0, size);

	storage.shortFilter = std::make_unique<HConvSingle>();
	filter->f_short = storage.shortFilter.get();
	hcInitSingle(filter->f_short, h, mflen, sflen, 1);

	storage.mediumFilter = std::make_unique<HConvDual>();
	filter->f_medium = storage.mediumFilter.get();
	hcInitDual(filter->f_medium, &(h[mflen]), hlen - mflen, mflen, lflen);
}


void hcCloseTripple(HConvTripple* filter)
{
	hcCloseSingle(filter->f_short);
	hcCloseDual(filter->f_medium);
	delete filter->storage;
	memset(filter, 0, sizeof(HConvTripple));
}
