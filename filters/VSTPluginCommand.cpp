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

bool splitChannelFill(const wstring& value, vector<wstring>& fill)
{
	fill.clear();
	size_t start = 0;
	while (true)
	{
		const size_t separator = value.find(L',', start);
		wstring element = separator == wstring::npos
			? value.substr(start) : value.substr(start, separator - start);
		if (element.empty())
			return false;
		fill.push_back(std::move(element));
		if (separator == wstring::npos)
			return true;
		start = separator + 1;
	}
}

bool validateChannelFill(const wchar_t* fillKey, const wchar_t* layoutKey, const vector<wstring>& fill,
	VST3BusLayout layout, wstring& error)
{
	if (fill.empty())
		return true;
	if (layout == VST3BusLayout::Auto)
	{
		error = wstring(fillKey) + L" requires an explicit " + layoutKey + L" layout";
		return false;
	}
	const size_t slotCount = (size_t)vst3BusLayoutChannelCount(layout);
	if (fill.size() != slotCount)
	{
		error = wstring(fillKey) + L" must name " + std::to_wstring(slotCount) + L" channels for the "
			+ vst3BusLayoutName(layout) + L" layout";
		return false;
	}
	return true;
}

wstring serializeChannelFill(const vector<wstring>& fill)
{
	wstring result;
	for (size_t slot = 0; slot < fill.size(); slot++)
	{
		if (slot != 0)
			result += L',';
		result += fill[slot];
	}
	return quoteCommandToken(result);
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
	const vector<wstring> parts = text::splitQuoted(parameters, L' ');
	bool sawInputKey = false;
	bool sawOutputKey = false;
	bool sawLayoutValue = false;
	bool sawDanglingBusKey = false;
	for (size_t i = 0; i < parts.size(); i += 2)
	{
		if (parts[i] == L"Input" || parts[i] == L"Output")
		{
			sawInputKey = sawInputKey || parts[i] == L"Input";
			sawOutputKey = sawOutputKey || parts[i] == L"Output";
			if (i + 1 < parts.size())
			{
				VST3BusLayout ignoredLayout;
				sawLayoutValue = sawLayoutValue || parseVST3BusLayout(parts[i + 1], ignoredLayout);
			}
			else
				sawDanglingBusKey = true;
		}
	}
	// Input and Output have always been legal plug-in parameter names. A single
	// numeric parameter (for example "Output 0.5") therefore stays on the legacy
	// path. A recognized layout value, or the presence of both reserved keys,
	// unambiguously opts into the new contract grammar.
	const bool hasBusKey = sawDanglingBusKey || sawLayoutValue || (sawInputKey && sawOutputKey);

	if (hasBusKey)
	{
		cmd.valid = false;
		if (parts.empty() || parts.size() % 2 != 0)
		{
			cmd.error = L"expected key/value pairs with Library, Input and Output";
			return cmd;
		}

		bool sawLibrary = false;
		bool sawInput = false;
		bool sawOutput = false;
		bool sawChunkData = false;
		bool sawInputChannels = false;
		bool sawOutputChannels = false;
		vector<wstring> inputChannelFill;
		vector<wstring> outputChannelFill;
		VST3BusContract contract;
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
				if (!parseVST3BusLayout(value, contract.input))
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
				if (!parseVST3BusLayout(value, contract.output))
				{
					cmd.error = L"unsupported Output layout \"" + value + L"\"";
					return cmd;
				}
			}
			else if (key == L"InputChannels")
			{
				if (sawInputChannels)
				{
					cmd.error = L"duplicate InputChannels key";
					return cmd;
				}
				sawInputChannels = true;
				if (!splitChannelFill(value, inputChannelFill))
				{
					cmd.error = L"InputChannels must be a comma-separated channel list";
					return cmd;
				}
			}
			else if (key == L"OutputChannels")
			{
				if (sawOutputChannels)
				{
					cmd.error = L"duplicate OutputChannels key";
					return cmd;
				}
				sawOutputChannels = true;
				if (!splitChannelFill(value, outputChannelFill))
				{
					cmd.error = L"OutputChannels must be a comma-separated channel list";
					return cmd;
				}
				for (size_t slot = 0; slot < outputChannelFill.size(); slot++)
				{
					if (outputChannelFill[slot] == L"-")
						continue;
					for (size_t other = slot + 1; other < outputChannelFill.size(); other++)
					{
						if (outputChannelFill[slot] == outputChannelFill[other])
						{
							cmd.error = L"duplicate OutputChannels entry \"" + outputChannelFill[slot] + L"\"";
							return cmd;
						}
					}
				}
			}
			else if (key == L"StereoInput")
			{
				cmd.error = L"StereoInput cannot be combined with Input/Output";
				return cmd;
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
		else if (validateChannelFill(L"InputChannels", L"Input", inputChannelFill, contract.input, cmd.error)
			&& validateChannelFill(L"OutputChannels", L"Output", outputChannelFill, contract.output, cmd.error))
		{
			cmd.busContract = contract;
			cmd.hasBusContract = true;
			cmd.inputChannels = std::move(inputChannelFill);
			cmd.outputChannels = std::move(outputChannelFill);
			cmd.valid = true;
		}
		return cmd;
	}

	cmd.libraryPath = resolveLibraryReference(extractLibraryReference(parameters));

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

	if (hasBusContract)
	{
		result += L" Input ";
		result += vst3BusLayoutName(busContract.input);
		if (!inputChannels.empty())
		{
			result += L" InputChannels ";
			result += serializeChannelFill(inputChannels);
		}
		result += L" Output ";
		result += vst3BusLayoutName(busContract.output);
		if (!outputChannels.empty())
		{
			result += L" OutputChannels ";
			result += serializeChannelFill(outputChannels);
		}
	}
	else if (stereoInput)
		result += L" StereoInput 1";
	result += serializeState(chunkData, paramMap);

	return result;
}
