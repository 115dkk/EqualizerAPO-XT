/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include "text/WideString.h"

#include <cstdio>
#include <cwctype>
#include <cerrno>
#include <cmath>

#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"
#include "VSTPluginCommand.h"

using std::vector;
using std::wstring;

namespace
{
wstring resolveLibraryReference(const wstring& libraryReference)
{
	if (libraryReference.empty())
		return L"";
	if (!PathIsRelativeW(libraryReference.c_str()))
		return libraryReference;

	wstring pluginPath = VSTPluginLibrary::getDefaultPluginPath();
	while (!pluginPath.empty() && (pluginPath.back() == L'\\' || pluginPath.back() == L'/'))
		pluginPath.pop_back();
	return pluginPath + L"\\" + libraryReference;
}

wstring quoteCommandToken(const wstring& token, bool force = false)
{
	if (!force && token.find_first_of(L" \t\"") == wstring::npos)
		return token;

	wstring result = L"\"";
	for (wchar_t c : token)
	{
		if (c == L'\"')
			result += L"\"\"";
		else
			result += c;
	}
	result += L"\"";
	return result;
}

bool parseFloatToken(const wstring& token, float& value)
{
	if (token.empty())
		return false;
	wchar_t* end = nullptr;
	errno = 0;
	value = wcstof(token.c_str(), &end);
	return errno != ERANGE && end != token.c_str() && *end == L'\0' && std::isfinite(value);
}

wstring serializeState(const wstring& chunkData, const std::unordered_map<wstring, float>& paramMap)
{
	wstring result;
	if (!chunkData.empty())
	{
		result += L" ChunkData ";
		result += quoteCommandToken(chunkData, true);
		return result;
	}

	for (const auto& it : paramMap)
	{
		wchar_t buffer[64];
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", (double)it.second);
		result += L" ";
		result += quoteCommandToken(it.first);
		result += L" ";
		result += buffer;
	}
	return result;
}
}

VSTPluginCommand VSTPluginCommand::parse(const wstring& /*configPath*/, const wstring& parameters)
{
	// configPath is intentionally unused: the parse never looks at it
	// (configPath only decides whether the factory loads the binary, which
	// stays in the factory). The engine factory and the Editor GUI share this
	// one grammar.
	VSTPluginCommand cmd;

	cmd.libraryPath = resolveLibraryReference(extractLibraryReference(parameters));

	vector<wstring> parts = text::splitQuoted(parameters, ' ');
	for (unsigned i = 0; i + 1 < parts.size(); i += 2)
	{
		wstring key = parts[i];
		wstring value = parts[i + 1];

		if (key == L"Library")
			continue;
		else if (key == L"ChunkData")
		{
			cmd.chunkData = value;
		}
		else if (key == L"StereoInput")
		{
			cmd.stereoInput = value == L"1" || value == L"true";
		}
		else
		{
			if (value.empty() || !std::iswdigit(value[0]))
			{
				size_t x = (size_t)i + 2;
				if (x < parts.size())
				{
					float f = wcstof(parts[x].c_str(), nullptr);
					cmd.paramMap[value.c_str()] = f;
				}
			}
			else
			{
				float f = wcstof(value.c_str(), nullptr);
				cmd.paramMap[key] = f;
			}
		}
	}

	return cmd;
}

std::wstring VSTPluginCommand::serialize() const
{
	// Mirrors the body VSTPluginFilterGUI::store() appends after the "Library
	// <path>" token. The Library token itself stays in store() because its
	// relative/absolute resolution uses Qt's QDir; this serializer owns only the
	// chunk/param body so a parse -> serialize round trip of that body is
	// lossless. The returned string carries a leading space so store() can
	// append it directly.
	wstring result;

	if (stereoInput)
		result += L" StereoInput 1";

	if (chunkData != L"")
	{
		result += L" ChunkData \"";
		result += chunkData;
		result += L"\"";
	}
	else
	{
		for (const auto& it : paramMap)
		{
			// Quote the name when it contains a space or a quote, doubling any
			// embedded quote, exactly as store() did with QString. splitQuoted
			// reverses this (a doubled "" inside quotes parses back to a single ").
			wstring name = it.first;
			if (name.find(L' ') != wstring::npos || name.find(L'"') != wstring::npos)
			{
				wstring escaped;
				for (wchar_t c : name)
				{
					if (c == L'"')
						escaped += L"\"\"";
					else
						escaped += c;
				}
				name = L"\"" + escaped + L"\"";
			}

			// QString("%1").arg(float) uses the 'g' format with the default six
			// significant digits; std::swprintf with "%g" on the double-promoted
			// value produces the same text in the C locale.
			wchar_t buffer[64];
			swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", (double)it.second);

			result += L" ";
			result += name;
			result += L" ";
			result += buffer;
		}
	}

	return result;
}

VST3BusCommand VST3BusCommand::parse(const wstring& /*configPath*/, const wstring& parameters)
{
	VST3BusCommand cmd;
	const vector<wstring> parts = text::splitQuoted(parameters, L' ');
	if (parts.empty() || parts.size() % 2 != 0)
	{
		cmd.error = L"expected key/value pairs with Library, Input and Output";
		return cmd;
	}

	bool sawLibrary = false;
	bool sawInput = false;
	bool sawOutput = false;
	bool sawChunkData = false;
	for (size_t i = 0; i < parts.size(); i += 2)
	{
		const wstring& key = parts[i];
		const wstring& value = parts[i + 1];
		if (key == L"Library")
		{
			if (sawLibrary)
			{
				cmd.error = L"duplicate Library key";
				return cmd;
			}
			sawLibrary = true;
			cmd.libraryPath = resolveLibraryReference(value);
			if (cmd.libraryPath.empty())
			{
				cmd.error = L"Library path must not be empty";
				return cmd;
			}
		}
		else if (key == L"Input")
		{
			if (sawInput)
			{
				cmd.error = L"duplicate Input key";
				return cmd;
			}
			sawInput = true;
			if (!parseVST3BusLayout(value, cmd.contract.input))
			{
				cmd.error = L"unsupported Input layout \"" + value + L"\"";
				return cmd;
			}
		}
		else if (key == L"Output")
		{
			if (sawOutput)
			{
				cmd.error = L"duplicate Output key";
				return cmd;
			}
			sawOutput = true;
			if (!parseVST3BusLayout(value, cmd.contract.output))
			{
				cmd.error = L"unsupported Output layout \"" + value + L"\"";
				return cmd;
			}
		}
		else if (key == L"ChunkData")
		{
			if (sawChunkData)
			{
				cmd.error = L"duplicate ChunkData key";
				return cmd;
			}
			sawChunkData = true;
			cmd.chunkData = value;
		}
		else
		{
			if (cmd.paramMap.find(key) != cmd.paramMap.end())
			{
				cmd.error = L"duplicate parameter key \"" + key + L"\"";
				return cmd;
			}
			float parameterValue = 0.0f;
			if (!parseFloatToken(value, parameterValue))
			{
				cmd.error = L"parameter \"" + key + L"\" requires a numeric value";
				return cmd;
			}
			cmd.paramMap.emplace(key, parameterValue);
		}
	}

	if (!sawLibrary)
		cmd.error = L"missing required Library key";
	else if (!sawInput)
		cmd.error = L"missing required Input key";
	else if (!sawOutput)
		cmd.error = L"missing required Output key";
	else if (sawChunkData && !cmd.paramMap.empty())
		cmd.error = L"ChunkData and parameter values cannot be combined";
	else
		cmd.valid = true;
	return cmd;
}

wstring VST3BusCommand::serialize() const
{
	if (!valid || libraryPath.empty())
		return L"";
	wstring result = L"Library ";
	result += quoteCommandToken(libraryPath);
	result += L" Input ";
	result += vst3BusLayoutName(contract.input);
	result += L" Output ";
	result += vst3BusLayoutName(contract.output);
	result += serializeState(chunkData, paramMap);
	return result;
}
