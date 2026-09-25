/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the engine host saw of a target's last stream, and where it keeps
	it: HKCU\SOFTWARE\EqualizerAPO\ASIO\{targetClsid}, under the user's hive
	so the host needs no elevation. The Device Selector's ASIO record reads
	the channel count and the sample rate back from here, because nothing
	else knows them before a DAW opens the device. The key and the value
	names are spelled here only; the writer (EngineHostCore) and the reader
	(AsioAPOInfo) both go through the registry port, so a fake registry
	pins the pair (audit #348 C7).
*/

#pragma once

#include <string>

#include "asio/StreamProcessor.h"
#include "services/registry/IRegistry.h"

namespace eapo::asio
{
	// The part of a stream's shape the record shows.
	struct StreamShape
	{
		unsigned long sampleRate = 0;                         // Hz, whole
		unsigned long channels[directionCount] = {0, 0};      // output, input
	};

	namespace StreamFacts
	{
		// HKEY_CURRENT_USER\SOFTWARE\EqualizerAPO\ASIO\{targetClsid}
		std::wstring key(const std::wstring& targetClsid);

		// Creates the key under format.deviceGuid and writes the stream's
		// shape. Throws RegistryError like every registry write; the host
		// treats the record as best effort and swallows it.
		void write(IRegistry& registry, const StreamFormat& format);

		// False when no stream was ever published for the target. A value
		// that is missing reads as 0.
		bool read(const IRegistry& registry, const std::wstring& targetClsid, StreamShape& shape);
	}
}
