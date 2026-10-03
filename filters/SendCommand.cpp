/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include <charconv>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include "filters/SendCommand.h"
#include "parser/NumericText.h"
#include "text/WideString.h"

namespace
{
	void setError(std::wstring* error, const std::wstring& reason)
	{
		if (error != nullptr)
			*error = reason;
	}

	bool endsWithInsensitive(const std::wstring& value, const std::wstring& suffix)
	{
		if (value.size() < suffix.size())
			return false;
		return text::toLower(value.substr(value.size() - suffix.size())) == suffix;
	}

	std::wstring formatFixed(double value, int precision)
	{
		// to_chars is locale-independent, so config text always uses a decimal
		// point. A finite double needs at most 309 integer digits plus punctuation.
		char buffer[512] = {};
		const std::to_chars_result converted = std::to_chars(
			buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, precision);
		if (converted.ec != std::errc())
			return L"";
		return std::wstring(buffer, converted.ptr);
	}

	std::wstring formatMilliseconds(double value)
	{
		std::wstring result = formatFixed(value, 3);
		while (!result.empty() && result.back() == L'0')
			result.pop_back();
		if (!result.empty() && result.back() == L'.')
			result.pop_back();
		return result;
	}

	std::wstring formatSamples(double value)
	{
		return formatFixed(value, 0);
	}

	std::wstring malformedTokenReason(const std::wstring& token)
	{
		return L"Invalid Send assignment or option: " + token;
	}
}

std::wstring SendCommand::canonicalEndpoint(const std::wstring& value)
{
	if (value.size() < 2 || value.front() != L'{' || value.back() != L'}')
		return L"";

	GUID guid = {};
	if (FAILED(CLSIDFromString(value.c_str(), &guid)))
		return L"";

	wchar_t buffer[40] = {};
	if (StringFromGUID2(guid, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0]))) == 0)
		return L"";
	return text::toLower(buffer);
}

bool SendCommand::parse(const std::wstring& command, const std::wstring& parameters,
	SendCommand& out, std::wstring* error)
{
	if (error != nullptr)
		error->clear();
	if (command != L"Send")
		return false;

	const std::vector<std::wstring> tokens = text::split(parameters, L' ');
	if (tokens.empty())
	{
		setError(error, L"Send requires an endpoint GUID and at least one assignment");
		return false;
	}

	SendCommand parsed;
	parsed.endpoint = canonicalEndpoint(tokens[0]);
	if (parsed.endpoint.empty())
	{
		setError(error, L"Send endpoint must be a brace-wrapped GUID: " + tokens[0]);
		return false;
	}

	bool latencySeen = false;
	bool compensateSeen = false;
	bool modeSeen = false;
	std::vector<std::wstring> assignmentTokens;
	assignmentTokens.reserve(tokens.size() - 1);

	for (size_t i = 1; i < tokens.size(); i++)
	{
		const std::wstring& token = tokens[i];
		const size_t equals = token.find(L'=');
		if (equals == std::wstring::npos)
		{
			setError(error, malformedTokenReason(token));
			return false;
		}

		const std::wstring key = text::toLower(token.substr(0, equals));
		const std::wstring value = token.substr(equals + 1);
		if (key == L"latency")
		{
			if (latencySeen)
			{
				setError(error, L"Latency given twice");
				return false;
			}
			latencySeen = true;

			std::wstring numberText = value;
			LatencyUnit unit = LatencyUnit::Milliseconds;
			if (endsWithInsensitive(value, L"samples"))
			{
				unit = LatencyUnit::Samples;
				numberText.resize(value.size() - 7);
			}
			else if (endsWithInsensitive(value, L"ms"))
			{
				unit = LatencyUnit::Milliseconds;
				numberText.resize(value.size() - 2);
			}

			const std::optional<double> number = numeric_text::parseNumber(numberText);
			if (!number || !std::isfinite(*number) || *number <= 0.0)
			{
				setError(error, L"Latency must be a positive finite number: " + value);
				return false;
			}
			if (unit == LatencyUnit::Samples && std::floor(*number) != *number)
			{
				setError(error, L"Latency in samples must be an integer: " + value);
				return false;
			}
			parsed.latencyUnit = unit;
			parsed.latency = *number;
		}
		else if (key == L"compensate")
		{
			if (compensateSeen)
			{
				setError(error, L"Compensate given twice");
				return false;
			}
			compensateSeen = true;
			const std::wstring lowered = text::toLower(value);
			if (lowered == L"true" || lowered == L"1")
				parsed.compensate = true;
			else if (lowered == L"false" || lowered == L"0")
				parsed.compensate = false;
			else
			{
				setError(error, L"Compensate must be true, false, 1, or 0: " + value);
				return false;
			}
		}
		else if (key == L"mode")
		{
			if (modeSeen)
			{
				setError(error, L"Mode given twice");
				return false;
			}
			modeSeen = true;
			const std::wstring lowered = text::toLower(value);
			if (lowered == L"mix")
				parsed.mode = Mode::Mix;
			else if (lowered == L"replace")
				parsed.mode = Mode::Replace;
			else
			{
				setError(error, L"Mode must be Mix or Replace: " + value);
				return false;
			}
		}
		else
		{
			assignmentTokens.push_back(token);
		}
	}

	if (assignmentTokens.empty())
	{
		setError(error, L"Send requires at least one channel assignment");
		return false;
	}

	const std::wstring assignmentText = text::join(assignmentTokens, L" ");
	parsed.assignments = parseCopyAssignments(assignmentText);
	if (parsed.assignments.size() != assignmentTokens.size())
	{
		// Find the first token Copy's parser drops so the diagnostic names it.
		for (const std::wstring& token : assignmentTokens)
		{
			if (parseCopyAssignments(token).size() != 1)
			{
				setError(error, malformedTokenReason(token));
				return false;
			}
		}
		setError(error, L"Send assignments are malformed");
		return false;
	}
	if (parsed.assignments.size() > maxChannels)
	{
		setError(error, L"Send supports at most 16 channel assignments");
		return false;
	}

	std::set<std::wstring> targets;
	for (size_t assignmentIndex = 0; assignmentIndex < parsed.assignments.size(); assignmentIndex++)
	{
		const Assignment& assignment = parsed.assignments[assignmentIndex];
		if (assignment.targetChannel.size() > maxTargetNameLength)
		{
			setError(error, L"Send target name is longer than 31 characters: " + assignment.targetChannel);
			return false;
		}
		if (!targets.insert(assignment.targetChannel).second)
		{
			setError(error, L"Duplicate Send target: " + assignment.targetChannel);
			return false;
		}
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			if (summand.channel.empty())
			{
				setError(error, L"Send carries channels, not constants: " + assignmentTokens[assignmentIndex]);
				return false;
			}
		}
	}

	out = std::move(parsed);
	return true;
}

std::wstring SendCommand::serialize() const
{
	std::wstring result = endpoint;
	const std::wstring assignmentText = serializeCopyAssignments(assignments);
	if (!assignmentText.empty())
		result += L" " + assignmentText;
	if (latencyUnit == LatencyUnit::Milliseconds)
		result += L" Latency=" + formatMilliseconds(latency) + L"ms";
	else if (latencyUnit == LatencyUnit::Samples)
		result += L" Latency=" + formatSamples(latency) + L"samples";
	if (!compensate)
		result += L" Compensate=false";
	if (mode == Mode::Replace)
		result += L" Mode=Replace";
	return result;
}
