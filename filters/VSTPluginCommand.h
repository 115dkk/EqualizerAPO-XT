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
#include <unordered_map>

// Plain description of a parsed "VSTPlugin:" config line. It holds exactly the
// three things VSTPluginFilterFactory::createFilter extracts from the parameter
// string before building a VSTPluginFilter: the resolved library PATH (already
// expanded relative-to-getDefaultPluginPath()), the optional base64 ChunkData,
// and the parameter map.
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

	// Qt-free parser for a "VSTPlugin:" parameter string: splitQuoted on spaces
	// into key/value pairs, "Library" resolved through the
	// relative-to-getDefaultPluginPath() logic and VSTPluginLibrary::getInstance(),
	// "ChunkData" stored as-is, and any other pair handled by the isdigit()
	// id-vs-name branch. configPath is accepted to match the factory's signature
	// but is unused here: the parse is configPath-independent (configPath
	// only gates the binary load, which stays in the factory).
	static VSTPluginCommand parse(const std::wstring& configPath, const std::wstring& parameters);

	// Re-creates the canonical parameter body that VSTPluginFilterGUI::store()
	// emits after the "Library <path>" token: either ChunkData "<base64>" or the
	// space-separated "<name> <value>" param list, with the same name quoting and
	// %g/QString::arg float formatting. The Library token itself stays in store()
	// because its relative/absolute path resolution depends on Qt's QDir, so this
	// serializer owns only the chunk/param body.
	std::wstring serialize() const;
};
