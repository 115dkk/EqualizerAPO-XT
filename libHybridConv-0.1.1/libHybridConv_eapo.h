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
// Adapted version for Equalizer APO. For original version see libHybridConv.h

#ifndef __LIBHYBRIDCONV_H__
#define __LIBHYBRIDCONV_H__


#include <fftw3.h>


/* Instance-owned runtime storage, defined in libHybridConv_eapo.cpp. A storage
 * object may retain an immutable filter bank shared with sibling channels;
 * mutable histories, mix buffers and FFTW plans always remain per instance. */
struct HConvSingleStorage;


typedef struct str_HConvSingle
{
	int step;			// processing step counter
	int maxstep;			// number of processing steps per audio frame
	int mixpos;			// current frame index
	int framelength;		// number of samples per audio frame
	int *steptask;			// processing tasks per step
	double *dft_time;		// DFT buffer (time domain)
	fftw_complex *dft_freq;	// DFT buffer (frequency domain)
	double *in_freq_real;		// input buffer (frequency domain)
	double *in_freq_imag;		// input buffer (frequency domain)
	int num_filterbuf;		// number of filter segments
	const double *const *filterbuf_freq_real;	// shared immutable filter segments (frequency domain)
	const double *const *filterbuf_freq_imag;	// shared immutable filter segments (frequency domain)
	int num_mixbuf;			// number of mixing segments		
	double **mixbuf_freq_real;	// mixing segments (frequency domain)
	double **mixbuf_freq_imag;	// mixing segments (frequency domain)
	double *history_time;		// history buffer (time domain)
	fftw_plan fft;			// FFT transformation plan
	fftw_plan ifft;		// IFFT transformation plan
	struct HConvSingleStorage *storage;	// owned buffers backing the pointers above
} HConvSingle;



/* single filter functions */
double hcTime(void);
void hcPutSingle(HConvSingle *filter, double*x);
void hcProcessSingle(HConvSingle *filter);
void hcGetSingle(HConvSingle *filter, double*y);
void hcGetAddSingle(HConvSingle *filter, double*y);
void hcInitSingle(HConvSingle *filter, const double* h, int hlen, int flen, int steps);
void hcInitSingleWithSharedFilterBank(HConvSingle *filter, const HConvSingle *prototype);
void hcCloseSingle(HConvSingle *filter);

/* The dual/tripple (low-latency) API is parked in
   libHybridConv_eapo_dormant.h; no project compiles it. */


#endif // __LIBHYBRIDCONV_H__
