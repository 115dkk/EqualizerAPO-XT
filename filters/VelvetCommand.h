/*
	This file is part of EqualizerAPO-XT.

	Grammar owner for the built-in sparse velvet-noise decorrelator.
*/

#pragma once

#include <string>

#include "filters/velvet/Processor.h"

struct VelvetCommand
{
	velvet::Parameters parameters;

	std::wstring serialize() const;

	// An empty parameter list is the documented default. Other lines consist
	// only of whitespace-separated key=value tokens. When error is supplied it
	// receives a user-facing reason for a Velvet line that cannot be applied.
	static bool parse(const std::wstring& command, const std::wstring& text,
		VelvetCommand& out, std::wstring* error = nullptr);
};
