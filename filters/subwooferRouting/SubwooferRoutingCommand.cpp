/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include "SubwooferRoutingCommand.h"

#include <cwctype>


namespace
{
constexpr unsigned Utf8CodePage = 65001;

bool fail(std::wstring* error)
{
	if (error != nullptr)
		*error = L"expected State or Profile";
	return false;
}

size_t skipWhitespace(const std::wstring& text, size_t position)
{
	while (position < text.size() && iswspace(text[position]))
		++position;
	return position;
}
}

std::string subwooferRoutingToUtf8(const std::wstring& text)
{
	return wintext::toNarrowString(text, Utf8CodePage);
}

bool SubwooferRoutingCommand::parse(const std::wstring& command,
	const std::wstring& text, SubwooferRoutingCommand& out, std::wstring* error)
{
	if (command != L"SubwooferRouting")
		return false;

	const size_t start = skipWhitespace(text, 0);
	if (start == text.size())
		return fail(error);

	Form parsedForm;
	size_t payloadPosition;
	if (text.compare(start, 5, L"State") == 0)
	{
		parsedForm = Form::State;
		payloadPosition = start + 5;
	}
	else if (text.compare(start, 7, L"Profile") == 0)
	{
		parsedForm = Form::Profile;
		payloadPosition = start + 7;
	}
	else
	{
		return fail(error);
	}

	if (payloadPosition >= text.size() || !iswspace(text[payloadPosition]))
		return fail(error);

	payloadPosition = skipWhitespace(text, payloadPosition);
	if (payloadPosition == text.size())
		return fail(error);

	std::wstring payload = text.substr(payloadPosition);
	if (parsedForm == Form::Profile)
		payload = text::trim(payload);
	if (payload.empty())
		return fail(error);

	out.form = parsedForm;
	out.payload = std::move(payload);
	return true;
}

std::wstring SubwooferRoutingCommand::serialize() const
{
	if (form == Form::Profile)
		return L"Profile " + payload;
	return L"State " + payload;
}
