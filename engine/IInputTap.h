/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>

#include "runtime/memory/AlignedMemory.h"

#pragma AVRT_VTABLES_BEGIN
class IInputTap
{
public:
	virtual ~IInputTap() = default;
	// Adds (or writes) received audio into the planar channels of one
	// configuration, after the block's virtual channels are zeroed and before
	// any filter runs. channels[0..channelCount) are the device channels in
	// FilterEngine::deviceChannelNames() order. Called once per configuration
	// per block: twice with the same blockToken while a configuration swap
	// crossfades, and the implementation must advance its own read cursor
	// only once per token. Realtime: no allocation, no locks, no waits.
	virtual void apply(double* const* channels, unsigned channelCount, unsigned frameCount,
		uint64_t blockToken) noexcept = 0;
	// True when the tap may add audio to a silent input block.
	virtual bool mayAddAudio() const noexcept = 0;
};
#pragma AVRT_VTABLES_END
