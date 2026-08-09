#include "stdafx.h"
#include "HilbertCommand.h"

#include <algorithm>
#include <cwctype>
#include <set>

#include "text/StringHelper.h"

namespace
{
bool fail(std::wstring* error, const std::wstring& reason)
{
	if (error != nullptr)
		*error = reason;
	return false;
}

bool validChannel(const std::wstring& value)
{
	if (value.empty() || value.size() > 16)
		return false;
	for (wchar_t c : value)
		if (!(iswalnum(c) || c == L'_' || c == L'-'))
			return false;
	return true;
}

bool parseChannels(const std::wstring& text, std::vector<std::wstring>& result)
{
	result.clear();
	std::set<std::wstring> seen;
	for (std::wstring channel : StringHelper::split(text, L','))
	{
		channel = StringHelper::toUpperCase(StringHelper::trim(channel));
		if (!validChannel(channel) || !seen.insert(channel).second)
			return false;
		result.push_back(channel);
	}
	if (result.size() > 1
		&& std::find(result.begin(), result.end(), L"ALL") != result.end())
		return false;
	return !result.empty();
}

std::wstring join(const std::vector<std::wstring>& channels)
{
	return StringHelper::join(channels, L",");
}
}

std::wstring HilbertCommand::serialize() const
{
	std::wstring result = L"Shift=" + join(shiftedChannels);
	if (!alignedChannels.empty())
		result += L" Align=" + join(alignedChannels);
	result += directionDegrees < 0 ? L" Direction=-90" : L" Direction=+90";
	return result;
}

bool HilbertCommand::parse(const std::wstring& command, const std::wstring& text,
	HilbertCommand& out, std::wstring* error)
{
	if (command != L"Hilbert")
		return false;

	out = HilbertCommand {};
	const std::wstring trimmed = StringHelper::trim(text);
	if (trimmed.empty())
		return true;

	bool sawShift = false;
	bool sawAlign = false;
	bool sawDirection = false;
	for (const std::wstring& token : StringHelper::split(trimmed, L' '))
	{
		if (token.empty())
			continue;
		const std::vector<std::wstring> pair = StringHelper::split(token, L'=');
		if (pair.size() != 2 || pair[0].empty() || pair[1].empty())
			return fail(error, L"expected Shift=..., Align=... and Direction=±90 settings");
		const std::wstring key = StringHelper::toLowerCase(pair[0]);
		if (key == L"shift")
		{
			if (sawShift)
				return fail(error, L"Shift appears more than once");
			sawShift = true;
			if (!parseChannels(pair[1], out.shiftedChannels))
				return fail(error, L"Shift must contain unique comma-separated channel names");
		}
		else if (key == L"align")
		{
			if (sawAlign)
				return fail(error, L"Align appears more than once");
			sawAlign = true;
			if (!parseChannels(pair[1], out.alignedChannels)
				|| std::find(out.alignedChannels.begin(),
					out.alignedChannels.end(), L"ALL") != out.alignedChannels.end())
				return fail(error, L"Align must contain unique explicit channel names");
		}
		else if (key == L"direction")
		{
			if (sawDirection)
				return fail(error, L"Direction appears more than once");
			sawDirection = true;
			if (pair[1] == L"-90")
				out.directionDegrees = -90;
			else if (pair[1] == L"+90" || pair[1] == L"90")
				out.directionDegrees = 90;
			else
				return fail(error, L"Direction must be -90 or +90");
		}
		else
		{
			return fail(error, L"unknown setting \"" + pair[0] + L"\"");
		}
	}

	for (const std::wstring& channel : out.shiftedChannels)
		if (std::find(out.alignedChannels.begin(),
			out.alignedChannels.end(), channel) != out.alignedChannels.end())
			return fail(error, L"a channel cannot be both Shift and Align");
	if (out.shiftedChannels.size() == 1 && out.shiftedChannels.front() == L"ALL"
		&& !out.alignedChannels.empty())
		return fail(error, L"Align cannot be used when Shift=ALL");
	return true;
}
