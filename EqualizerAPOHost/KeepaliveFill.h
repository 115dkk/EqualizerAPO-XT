/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>

// Float32 or signed PCM16/24/32, including extensible valid-bit containers.
bool keepaliveFormatSupported(const WAVEFORMATEX& format);

// Writes exactly frames * nBlockAlign bytes. Unsupported formats get zeros.
// TPDF peaks at 1e-5 full scale before PCM quantization; stochastic rounding
// preserves sub-LSB noise (16-bit PCM therefore occasionally reaches +/-1 LSB).
// Extensible descriptors must include their cbSize bytes after WAVEFORMATEX.
void fillKeepalive(BYTE* data, UINT32 frames, const WAVEFORMATEX& format, bool dither, uint32_t& rngState);
