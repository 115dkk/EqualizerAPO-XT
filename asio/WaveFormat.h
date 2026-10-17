/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

namespace eapo::asio::waveformat
{
	struct Container
	{
		bool floating = false;
		unsigned bits = 0;
		unsigned validBits = 0;
	};

	inline bool containerOf(const WAVEFORMATEX* format, Container& out) noexcept
	{
		if (format == nullptr)
			return false;
		out = {};
		out.bits = format->wBitsPerSample;
		out.validBits = format->wBitsPerSample;
		if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			if (format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
				return false;
			const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
			if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
				out.floating = true;
			else if (ext->SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
				return false;
			if (ext->Samples.wValidBitsPerSample != 0)
				out.validBits = ext->Samples.wValidBitsPerSample;
		}
		else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
		{
			out.floating = true;
		}
		else if (format->wFormatTag != WAVE_FORMAT_PCM)
		{
			return false;
		}
		return out.validBits > 0 && out.validBits <= out.bits;
	}
}
