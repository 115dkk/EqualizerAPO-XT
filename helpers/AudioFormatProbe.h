/*
	This file is part of EqualizerAPO, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <string>

// Probe a Windows audio endpoint to find out whether EqualizerAPO can
// natively process its stream or will fall back to passthrough.
//
// Background: the APO only processes streams whose connection format is
// IEEE_FLOAT with a 4-byte (float32) or 8-byte (float64) container. Any
// other subtype (PCM integer, AC-3, etc.) is forwarded unchanged through
// the passthrough path so audio still reaches the device. The Editor uses
// this helper to surface that distinction in the UI — the user otherwise
// has no way to tell that filters are silently inactive on a given device.
namespace AudioFormatProbe
{
	enum class Status
	{
		Unknown,            // Could not query the device (offline, no permission, etc.)
		ActiveFloat32,      // IEEE_FLOAT, 4-byte container — EQ applied natively
		ActiveFloat64,      // IEEE_FLOAT, 8-byte container — EQ applied natively
		Passthrough,        // Any other format — APO forwards untouched
	};

	struct Result
	{
		Status status = Status::Unknown;
		unsigned containerBytes = 0;
		unsigned validBits = 0;
		unsigned sampleRate = 0;
		unsigned channelCount = 0;
		bool isFloat = false;
		std::wstring subtypeDescription;  // human-readable, e.g. "IEEE_FLOAT", "PCM"
	};

	// Query the given endpoint's current mix format via IMMDevice / IAudioClient.
	// deviceGuid must be the endpoint ID string ("{...}"). Returns Status::Unknown
	// if the endpoint cannot be queried.
	Result probe(const std::wstring& deviceGuid);

	// Short one-line description suitable for a status-bar label or tooltip.
	std::wstring describe(const Result& r);

	// True when the EQ chain is bypassed for this stream.
	inline bool isPassthrough(Status s) { return s == Status::Passthrough; }
}
