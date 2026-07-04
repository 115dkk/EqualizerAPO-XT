/***************************************************************************
 *   Copyright (C) 2009 by Christian Borss                                 *
 *   christian.borss@rub.de                                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

// DORMANT CODE - deliberately compiled by NO project.
//
// The dual/triple-segment (low-latency) convolver API was ported through the
// double-precision rewrite but has no callers: every filter in the engine
// uses the Single API. It is kept - explicitly parked, not forgotten debt -
// because a low-latency mode for heavy convolution stacks (and a possible
// multicore split) is planned future work (maintainer decision 2026-07-04,
// audit #146 TD009). To revive: add this .cpp to Common.vcxproj and
// Editor/Editor.pro next to libHybridConv_eapo.cpp, include the _dormant.h
// where needed, and rerun hcBenchmarkDual/Tripple before first use.

#ifndef LibHybridConv_eapo_dormant_h
#define LibHybridConv_eapo_dormant_h

#include "libHybridConv_eapo.h"

struct HConvDualStorage;
struct HConvTrippleStorage;

typedef struct str_HConvDual
{
	int step;		// processing step counter
	int maxstep;		// number of processing steps per long audio frame
	int flen_long;		// number of samples per long audio frame
	int flen_short;		// number of samples per short audio frame
	double *in_long;		// input buffer (long frame)
	double *out_long;	// output buffer (long frame)
	HConvSingle *f_long;	// convolution filter (long segments)
	HConvSingle *f_short;	// convolution filter (short segments)
	struct HConvDualStorage *storage;	// owned buffers backing the pointers above
} HConvDual;


typedef struct str_HConvTripple
{
	int step;		// processing step counter
	int maxstep;		// number of processing steps per long audio frame
	int flen_medium;	// number of samples per long audio frame
	int flen_short;		// number of samples per short audio frame
	double *in_medium;	// input buffer (long frame)
	double *out_medium;	// output buffer (long frame)
	HConvDual *f_medium;	// convolution filter (long segments)
	HConvSingle *f_short;	// convolution filter (short segments)
	struct HConvTrippleStorage *storage;	// owned buffers backing the pointers above
} HConvTripple;

/* dual filter functions */
void hcBenchmarkDual(int sflen, int lflen);
void hcProcessDual(HConvDual *filter, double*in, double*out);
void hcProcessAddDual(HConvDual *filter, double*in, double*out);
void hcInitDual(HConvDual *filter, double*h, int hlen, int sflen, int lflen);
void hcCloseDual(HConvDual *filter);

/* tripple filter functions */
void hcBenchmarkTripple(int sflen, int mflen, int lflen);
void hcProcessTripple(HConvTripple *filter, double*in, double*out);
void hcProcessAddTripple(HConvTripple *filter, double*in, double*out);
void hcInitTripple(HConvTripple *filter, double*h, int hlen, int sflen, int mflen, int lflen);
void hcCloseTripple(HConvTripple *filter);

#endif
