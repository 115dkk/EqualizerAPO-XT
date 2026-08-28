/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

// How BiQuadFilter::process splits the channel list across its kernels.
// The lane width is a parameter instead of hn::Lanes so the planning logic is
// testable for SIMD widths the test host cannot execute (an AVX-512 plan can
// be checked on an AVX2 machine).
//
// The SIMD group assignment (leading floor(channelCount / laneWidth) *
// laneWidth channels) is part of the audio regression contract: the SIMD
// kernel uses FMA where the scalar kernels do not, so moving a channel
// between the SIMD group and the scalar kernels changes that variant's
// reference output. Remainder channels may move freely between the paired
// and single kernels, which share the scalar arithmetic bit for bit.
struct BiQuadKernelPlan
{
	unsigned simdChannels;   // leading channels, Highway groups of laneWidth
	unsigned pairedChannels; // next channels, two dependency chains at a time
	unsigned singleChannels; // trailing channels, one at a time
};

inline BiQuadKernelPlan planBiQuadKernels(unsigned channelCount, unsigned laneWidth)
{
	BiQuadKernelPlan plan = {};
	if (laneWidth >= 1)
		plan.simdChannels = channelCount / laneWidth * laneWidth;
	const unsigned rest = channelCount - plan.simdChannels;
	plan.pairedChannels = rest / 2 * 2;
	plan.singleChannels = rest - plan.pairedChannels;
	return plan;
}
