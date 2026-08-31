/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "text/WideString.h"
#include "parser/NumericText.h"
#include "VelvetCommand.h"

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <limits>
#include <set>


namespace
{
std::wstring number(double value)
{
	wchar_t buffer[64];
	swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.10g", value);
	return buffer;
}

bool suffixNumber(const std::wstring& source, const std::wstring& suffix,
	double& out)
{
	std::wstring text = text::trim(source);
	const std::wstring lower = text::toLower(text);
	if (!suffix.empty())
	{
		if (!lower.ends_with(suffix))
			return false;
		text.resize(text.size() - suffix.size());
	}
	if (text.empty())
		return false;
	// Audit #250 F015: accept the decimal comma like the BiQuad family.
	text = numeric_text::normalizeDecimalComma(text);
	wchar_t* end = nullptr;
	out = wcstod(text.c_str(), &end);
	return end != text.c_str() && *end == L'\0' && std::isfinite(out);
}

bool fail(std::wstring* error, const std::wstring& reason)
{
	if (error != nullptr)
		*error = reason;
	return false;
}
}

std::wstring VelvetCommand::serialize() const
{
	std::wstring result = L"Mode=";
	result += parameters.dynamic ? L"Dynamic" : L"Static";
	result += L" Amount=" + number(parameters.amount * 100.0) + L"%";
	result += L" Length=" + number(parameters.lengthMs) + L"ms";
	result += L" Density=" + number(parameters.density) + L"/s";
	result += L" Evolution=" + number(parameters.refreshSeconds) + L"s";
	result += L" Transition=" + number(parameters.transitionMs) + L"ms";
	result += L" Decay=" + number(parameters.decayDb) + L"dB";
	result += L" Variation=" + std::to_wstring(parameters.seed);
	return result;
}

bool VelvetCommand::parse(const std::wstring& command, const std::wstring& text,
	VelvetCommand& out, std::wstring* error)
{
	if (command != L"Velvet")
		return false;

	out = VelvetCommand {};
	const std::wstring trimmed = text::trim(text);
	if (trimmed.empty())
		return true;

	std::set<std::wstring> seen;
	for (const std::wstring& token : text::split(trimmed, L' '))
	{
		if (token.empty())
			continue;
		const std::vector<std::wstring> pair = text::split(token, L'=');
		if (pair.size() != 2 || pair[0].empty() || pair[1].empty())
			return fail(error, L"expected whitespace-separated key=value settings");

		const std::wstring key = text::toLower(pair[0]);
		if (!seen.insert(key).second)
			return fail(error, L"setting \"" + pair[0] + L"\" appears more than once");

		double value = 0.0;
		if (key == L"mode")
		{
			const std::wstring mode = text::toLower(pair[1]);
			if (mode == L"dynamic")
				out.parameters.dynamic = true;
			else if (mode == L"static")
				out.parameters.dynamic = false;
			else
				return fail(error, L"Mode must be Static or Dynamic");
		}
		else if (key == L"amount")
		{
			if (!suffixNumber(pair[1], L"%", value) || value < 0.0 || value > 100.0)
				return fail(error, L"Amount must be between 0% and 100%");
			out.parameters.amount = value / 100.0;
		}
		else if (key == L"length")
		{
			if (!suffixNumber(pair[1], L"ms", value) || value < 1.0 || value > 100.0)
				return fail(error, L"Length must be between 1ms and 100ms");
			out.parameters.lengthMs = value;
		}
		else if (key == L"density")
		{
			if (!suffixNumber(pair[1], L"/s", value) || value < 100.0 || value > 4000.0)
				return fail(error, L"Density must be between 100/s and 4000/s");
			out.parameters.density = value;
		}
		else if (key == L"evolution")
		{
			if (!suffixNumber(pair[1], L"s", value) || value < 0.1 || value > 60.0)
				return fail(error, L"Evolution must be between 0.1s and 60s");
			out.parameters.refreshSeconds = value;
		}
		else if (key == L"transition")
		{
			if (!suffixNumber(pair[1], L"ms", value) || value < 1.0 || value > 2000.0)
				return fail(error, L"Transition must be between 1ms and 2000ms");
			out.parameters.transitionMs = value;
		}
		else if (key == L"decay")
		{
			if (!suffixNumber(pair[1], L"db", value) || value < -120.0 || value > 0.0)
				return fail(error, L"Decay must be between -120dB and 0dB");
			out.parameters.decayDb = value;
		}
		else if (key == L"variation")
		{
			const std::wstring& seedText = pair[1];
			wchar_t* end = nullptr;
			errno = 0;
			const unsigned long long seed = wcstoull(seedText.c_str(), &end, 10);
			if (end == seedText.c_str() || *end != L'\0' || errno == ERANGE
				|| seed < 1 || seed > 4294967295ULL)
				return fail(error, L"Variation must be an integer from 1 to 4294967295");
			out.parameters.seed = static_cast<std::uint64_t>(seed);
		}
		else
		{
			return fail(error, L"unknown setting \"" + pair[0] + L"\"");
		}
	}

	if (out.parameters.transitionMs > out.parameters.refreshSeconds * 900.0)
		return fail(error, L"Transition must not exceed 90% of Evolution");
	return true;
}
