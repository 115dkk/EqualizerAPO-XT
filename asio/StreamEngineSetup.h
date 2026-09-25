/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The engine one lane of an ASIO stream runs: what the in-process adapter
	(InProcProcessor) and the engine host (EngineHostCore) both build, so the
	two adapters hash identically by construction instead of by a comment
	that asks for it (audit #348 F12). Kept out of StreamProcessor.h because
	it needs the engine's headers, which the wrapper DLL does not carry.
	The probe computes its reference setup on its own on purpose: it is the
	independent oracle both adapters are checked against.
*/

#pragma once

#include <string>

#include "asio/StreamProcessor.h"
#include "engine/FilterEngine.h"

namespace eapo::asio
{
	// One lane: as many channels in as out, no mask, the stream's buffer as
	// the largest block, post-mix, capture for the input lane, and the
	// target's name and CLSID for the Device: line under the connection
	// name "ASIO". An empty configPath is the registry's ConfigPath with the
	// change watcher.
	inline EngineSetup streamEngineSetup(const StreamFormat& format, Direction direction, const std::wstring& configPath)
	{
		const unsigned channels = format.channelCount(direction);
		return EngineSetup{
			.sampleRate = static_cast<float>(format.sampleRate),
			.inputChannelCount = channels,
			.realChannelCount = channels,
			.outputChannelCount = channels,
			.channelMask = 0,
			.maxFrameCount = format.frames,
			.customPath = configPath,
			.preMix = false,
			.capture = direction == Direction::Input,
			.postMixInstalled = true,
			.deviceName = format.deviceName,
			.connectionName = L"ASIO",
			.deviceGuid = format.deviceGuid
		};
	}
}
