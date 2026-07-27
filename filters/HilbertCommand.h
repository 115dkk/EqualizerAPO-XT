/*
	This file is part of EqualizerAPO-XT.

	Grammar owner for the built-in linear-phase Hilbert transformer.
*/

#pragma once

#include <string>
#include <vector>

struct HilbertCommand
{
	std::vector<std::wstring> shiftedChannels {L"ALL"};
	std::vector<std::wstring> alignedChannels;
	int directionDegrees = -90;

	std::wstring serialize() const;
	static bool parse(const std::wstring& command, const std::wstring& text,
		HilbertCommand& out, std::wstring* error = nullptr);
};
