/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

// A file that a configuration line names, from the text as written to the
// file the engine may open (audit #348 A1). Include, Convolution,
// MultiConvolution and SubwooferRouting's Profile read their file argument by
// one rule, and the Editor's cards and its import scanner resolve with the
// same functions, so the Editor shows and copies the file the engine opens.
//
// VSTPlugin keeps a rule of its own, older than this module: its tokenizer
// takes the quotes, nothing is expanded, and a relative reference is taken
// from the plug-in folder rather than the configuration's. resolveLibrary
// holds that rule so the two sit side by side.
class ConfigFileReference
{
public:
	// The written text as the engine reads it: surrounding whitespace and one
	// pair of double quotes removed, then %VARIABLES% expanded.
	static std::wstring normalize(const std::wstring& written);

	// The absolute, lexically normal path the written text names. Relative
	// text is taken from the folder of configPath. Empty when nothing is
	// written.
	static std::wstring resolve(const std::wstring& configPath, const std::wstring& written);

	// VSTPlugin's Library reference: an absolute reference as written, a
	// relative one below pluginFolder. Empty when nothing is written.
	static std::wstring resolveLibrary(const std::wstring& pluginFolder, const std::wstring& reference);

	// The file a line names and whether the engine may open it.
	struct Target
	{
		// resolve()'s result; empty when nothing is written and when the
		// engine may not open it.
		std::wstring path;
		// Why the engine will not open path (ConfigPathPolicy); empty when it
		// may.
		std::wstring refusal;
	};

	// resolve() judged by ConfigPathPolicy::allowsOpen. A factory that opens a
	// file a line names takes the path from here, so it cannot hold one that
	// was not judged.
	static Target target(const std::wstring& configPath, const std::wstring& written);
};
