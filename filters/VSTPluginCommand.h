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

#pragma once

#include <string>
#include "text/WideString.h"
#include <unordered_map>
#include <vector>
#include "vst/VST3BusLayout.h"


// Plain description of a parsed "VSTPlugin:" config line. It holds exactly the
// state VSTPluginFilterFactory::createFilter extracts from the parameter string
// before building a VSTPluginFilter: the resolved library PATH (already expanded
// relative-to-getDefaultPluginPath()), the optional VST3 main-bus contract, the
// optional base64 ChunkData, and the parameter map.
//
// The struct stays free of the VST host headers on purpose (it carries only the
// parsed path string), so it can be parse/serialize round-trip tested without
// pulling in VSTPluginLibrary/VSTPluginInstance. Callers resolve the path to a
// cached VSTPluginLibrary via VSTPluginLibrary::getInstance() themselves; the
// binary is loaded lazily only in VSTPluginLibrary::initialize(), so parsing
// never touches a plugin DLL.
struct VSTPluginCommand
{
	std::wstring libraryPath;
	std::wstring chunkData;
	std::unordered_map<std::wstring, float> paramMap;
	// "Input <layout> Output <layout>" is an explicit main-bus contract for
	// VST3. The factory deliberately discards it after loading a VST2 module.
	VST3BusContract busContract;
	bool hasBusContract = false;
	std::vector<std::wstring> inputChannels;
	std::vector<std::wstring> outputChannels;
	// "StereoInput 1": negotiate a stereo input bus with the full-width output
	// bus. This shipped before explicit Input/Output layouts and remains readable
	// for existing configurations; new configurations should use busContract.
	bool stereoInput = false;
	bool valid = true;
	std::wstring error;

	// Returns the value paired with the Library key without resolving or
	// loading it. Import/migration uses this to retain VST binaries as external
	// references while copying only the config and its portable dependencies.
	static std::wstring extractLibraryReference(const std::wstring& parameters)
	{
		const std::vector<std::wstring> parts = text::splitQuoted(parameters, L' ');
		for (size_t i = 0; i + 1 < parts.size(); i += 2)
		{
			if (parts[i] == L"Library")
				return parts[i + 1];
		}
		return L"";
	}

	// Qt-free parser for a "VSTPlugin:" parameter string. Existing lines without
	// Input/Output retain the legacy permissive parameter grammar. If either bus
	// key is present, both are required and the complete key/value list is parsed
	// strictly so malformed layout contracts can be reported before loading a
	// plug-in module. configPath is accepted to match the factory's signature but
	// is unused here; only the factory loads the binary.
	static VSTPluginCommand parse(const std::wstring& configPath, const std::wstring& parameters);

	// Re-creates the canonical parameter body after the "Library <path>" token:
	// the optional Input/Output contract followed by either ChunkData "<base64>"
	// or the space-separated "<name> <value>" parameter list. The Library token
	// stays in the Editor because its relative/absolute path resolution depends
	// on Qt's QDir.
	std::wstring serialize() const;
};
