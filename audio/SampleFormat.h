/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Which sample formats the APO processes. The APO decides it from the
	connection format it is given (apo::detectSampleFormat), and the Editor
	predicts it from the endpoint's shared-mode mix format
	(AudioFormatProbe). The shared-mode mix format is the audio engine's own
	format, the one a system-effect APO receives, so both ask the same
	question and must give the same answer (audit #348 F19).
*/

#pragma once

#include <cstddef>

namespace audio
{

enum class SampleFormat
{
	Unsupported = 0,
	Float32 = 1,
	Float64 = 2
};

// IEEE float in a 4- or 8-byte container is processed; everything else is
// passed through. Only the container size counts: some virtual devices
// report non-canonical valid-bit counts for a plain 32-bit float container.
inline SampleFormat sampleFormatFor(bool isIeeeFloat, size_t containerBytes)
{
	if (isIeeeFloat)
	{
		if (containerBytes == 4)
			return SampleFormat::Float32;
		if (containerBytes == 8)
			return SampleFormat::Float64;
	}
	return SampleFormat::Unsupported;
}

}
