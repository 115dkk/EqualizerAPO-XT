/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

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

#include <memory>
#include <vector>

#include "ConfigFileReference.h"

// Decoded impulse-response PCM, shared between filters that reference the same
// IR file. Each live filter keeps a shared_ptr to its entry; the process-wide
// cache (IrCache.cpp) holds only weak references, so an entry survives exactly
// as long as some filter still uses it.
struct IrCacheEntry
{
	unsigned channels = 0;
	unsigned frames = 0;
	// Channel-major IR samples; each inner vector has `frames` elements.
	std::vector<std::vector<double>> buffers;
};

// Loads the impulse response at `filename` (or returns the cached copy, keyed
// by path + mtime + sample rate), validated against the device sample rate.
// Returns nullptr and logs when the file is unreadable, the sample rate does
// not match, or the file has no usable audio (0 frames / 0 channels /
// frames > INT_MAX). One implementation shared by ConvolutionFilter and
// MultiConvolutionFilter so the intake hardening and the cache cannot
// diverge between them.
std::shared_ptr<const IrCacheEntry> loadIrCached(const JudgedPath& filename, double sampleRate);
