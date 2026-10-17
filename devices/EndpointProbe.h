/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>

// Endpoint COM operations are separate from registry records and transactions.
// Tests supply a probe without querying or activating an audio device.
class EndpointProbe
{
public:
	virtual ~EndpointProbe() = default;
	virtual std::wstring defaultEndpointGuid(bool input, int role = 1) const = 0;
	virtual void probeEndpoint(const std::wstring& guid, bool input, const std::wstring& name) const = 0;
};

const EndpointProbe& systemEndpointProbe();
