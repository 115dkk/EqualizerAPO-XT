/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#include "stdafx.h"

#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <Winternl.h>

#include "platform/windows/Win32Resource.h"
#include "ProcessCommandLine.h"

namespace
{
typedef NTSTATUS (NTAPI* pfnNtQueryInformationProcess)(
	IN HANDLE ProcessHandle,
	IN PROCESSINFOCLASS ProcessInformationClass,
	OUT PVOID ProcessInformation,
	IN ULONG ProcessInformationLength,
	OUT PULONG ReturnLength OPTIONAL
	);

// SeDebugPrivilege for as long as this lives. The token's previous state is
// kept and put back, so a privilege that was off is off again afterwards; a
// token that does not hold the privilege is left as it is (AdjustTokenPrivileges
// then succeeds with ERROR_NOT_ALL_ASSIGNED and changes nothing).
class DebugPrivilegeScope
{
public:
	DebugPrivilegeScope()
	{
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()))
			throw std::runtime_error("Error in OpenProcessToken while enabling SeDebugPrivilege for process inspection");

		LUID luid;
		if (!LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &luid))
			throw std::runtime_error("Error in LookupPrivilegeValue while enabling SeDebugPrivilege for process inspection");

		TOKEN_PRIVILEGES enable;
		enable.PrivilegeCount = 1;
		enable.Privileges[0].Luid = luid;
		enable.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		DWORD previousSize = 0;
		if (!AdjustTokenPrivileges(token.get(), FALSE, &enable, sizeof(previous), &previous, &previousSize))
			throw std::runtime_error("Error in AdjustTokenPrivileges while enabling SeDebugPrivilege for process inspection");
		adjusted = true;
	}

	~DebugPrivilegeScope()
	{
		// previous lists only what the call above changed, so restoring it is
		// a no-op for a privilege that was already on.
		if (adjusted)
			AdjustTokenPrivileges(token.get(), FALSE, &previous, 0, nullptr, nullptr);
	}

	DebugPrivilegeScope(const DebugPrivilegeScope&) = delete;
	DebugPrivilegeScope& operator=(const DebugPrivilegeScope&) = delete;

private:
	winutil::UniqueHandle token;
	TOKEN_PRIVILEGES previous = {};
	bool adjusted = false;
};
}

namespace winutil
{
std::vector<ProcessWithCommandLine> findProcessesByExeName(const wchar_t* exeFileName)
{
	// Names first: the snapshot needs no privilege, and most of the time no
	// process of that name runs at all.
	std::vector<DWORD> matches;
	{
		winutil::UniqueHandle snapshotHandle(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		if (!snapshotHandle)
			throw std::runtime_error("Could not take a snapshot of all processes");
		PROCESSENTRY32W entry;
		entry.dwSize = sizeof(PROCESSENTRY32W);
		for (bool loop = Process32FirstW(snapshotHandle.get(), &entry) != 0; loop;
			loop = Process32NextW(snapshotHandle.get(), &entry) != 0)
		{
			if (_wcsicmp(entry.szExeFile, exeFileName) == 0)
				matches.push_back(entry.th32ProcessID);
		}
	}
	if (matches.empty())
		return {};

	DebugPrivilegeScope privilege;

	winutil::UniqueModule module(LoadLibraryW(L"ntdll.dll"));
	if (!module)
		throw std::runtime_error("Could not load ntdll.dll");

	pfnNtQueryInformationProcess NtQueryInformationProcess =
		reinterpret_cast<pfnNtQueryInformationProcess>(GetProcAddress(module.get(), "NtQueryInformationProcess"));
	if (NtQueryInformationProcess == nullptr)
		throw std::runtime_error("Function NtQueryInformationProcess not found in ntdll.dll");

	std::vector<ProcessWithCommandLine> result;
	for (const DWORD processId : matches)
	{
		winutil::UniqueHandle processHandle(OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE,
			FALSE, processId));
		if (!processHandle)
			throw std::runtime_error("Could not open process");

		PROCESS_BASIC_INFORMATION basicInformation;
		NTSTATUS status = NtQueryInformationProcess(processHandle.get(), ProcessBasicInformation,
			&basicInformation, sizeof(basicInformation), nullptr);
		if (status < 0)
			throw std::runtime_error("Could not query process information");

		_PEB peb;
		if (!ReadProcessMemory(processHandle.get(), basicInformation.PebBaseAddress,
			&peb, sizeof(peb), nullptr))
			throw std::runtime_error("Could not read peb from process memory");

		RTL_USER_PROCESS_PARAMETERS processParams;
		if (!ReadProcessMemory(processHandle.get(), peb.ProcessParameters,
			&processParams, sizeof(processParams), nullptr))
			throw std::runtime_error("Could not read process parameters from process memory");

		std::vector<wchar_t> cmdLineBuf(processParams.CommandLine.Length / sizeof(wchar_t));
		if (!ReadProcessMemory(processHandle.get(), processParams.CommandLine.Buffer,
			cmdLineBuf.data(), processParams.CommandLine.Length, nullptr))
			throw std::runtime_error("Could not read command line from process memory");

		ProcessWithCommandLine found;
		found.processId = processId;
		found.commandLine.assign(cmdLineBuf.data(), cmdLineBuf.size());
		result.push_back(std::move(found));
	}

	return result;
}

void requestProcessClose(unsigned long processId)
{
	winutil::UniqueHandle snapshotHandle(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
	if (!snapshotHandle)
		throw std::runtime_error("Could not take a snapshot of all processes");

	THREADENTRY32 entry;
	entry.dwSize = sizeof(THREADENTRY32);
	bool loop = Thread32First(snapshotHandle.get(), &entry) != 0;
	while (loop)
	{
		if (entry.th32OwnerProcessID == processId)
			// will just fail for threads not having a message queue
			PostThreadMessageW(entry.th32ThreadID, WM_QUIT, 0, 0);

		loop = Thread32Next(snapshotHandle.get(), &entry) != 0;
	}
}
}
