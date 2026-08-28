/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <string>
#include <vector>

// Process inspection utilities (audit #275 C5): reading another process's
// command line means enabling SeDebugPrivilege and walking its PEB, which is
// Windows plumbing with no relation to audio devices - it used to live as
// ninety inline lines of devices/VoicemeeterAPOInfo.cpp, throwing
// RegistryError for token failures because the messages were copied from the
// registry ownership code.
namespace winutil
{
	struct ProcessWithCommandLine
	{
		unsigned long processId = 0;
		std::wstring commandLine;
	};

	// Answers every running process whose executable file name equals
	// exeFileName (case-sensitive, name only - no path), with the command
	// line read from its PEB. Enables SeDebugPrivilege on the calling
	// process's token first. Throws std::runtime_error when the privilege
	// cannot be enabled or a matching process refuses inspection.
	std::vector<ProcessWithCommandLine> findProcessesByExeName(const wchar_t* exeFileName);

	// Asks a process to close by posting WM_QUIT to each of its threads
	// (threads without a message queue simply ignore it). Throws
	// std::runtime_error when the thread snapshot cannot be taken.
	void requestProcessClose(unsigned long processId);
}
