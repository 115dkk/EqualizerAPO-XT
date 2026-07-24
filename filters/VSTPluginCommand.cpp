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

#include <cstdio>

#include "helpers/StringHelper.h"
#include "helpers/VSTPluginLibrary.h"
#include "VSTPluginCommand.h"

using std::vector;
using std::wstring;

VSTPluginCommand VSTPluginCommand::parse(const wstring& /*configPath*/, const wstring& parameters)
{
	// configPath is intentionally unused: the parse never looks at it
	// (configPath only decides whether the factory loads the binary, which
	// stays in the factory). The engine factory and the Editor GUI share this
	// one grammar.
	VSTPluginCommand cmd;

	wstring libraryReference = extractLibraryReference(parameters);
	if (!libraryReference.empty())
	{
		if (PathIsRelativeW(libraryReference.c_str()))
		{
			wchar_t filePath[MAX_PATH];
			wstring pluginPath = VSTPluginLibrary::getDefaultPluginPath();
			pluginPath._Copy_s(filePath, sizeof(filePath) / sizeof(wchar_t), MAX_PATH);
			if (pluginPath.size() < MAX_PATH)
				filePath[pluginPath.size()] = L'\0';
			else
				filePath[MAX_PATH - 1] = L'\0';
			PathAppendW(filePath, libraryReference.c_str());
			cmd.libraryPath = filePath;
		}
		else
			cmd.libraryPath = libraryReference;
	}

	vector<wstring> parts = StringHelper::splitQuoted(parameters, ' ');
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
			if (!isdigit(value.c_str()[0]))
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
