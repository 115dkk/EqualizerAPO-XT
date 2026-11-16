/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "CrashHandler.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include "platform/windows/Win32Resource.h"
#include "version.h"

namespace
{
// UI and loader writers serialize with a mutex; the crash handler NEVER
// takes it (it may have interrupted its owner). Atomic characters avoid C++
// data races, and an odd/even sequence makes each bounded read either a
// complete publication or the fixed fallback, not a mix of two actions.
// seq_cst orders the character copies with both sequence reads. No allocation,
// lock or unbounded retry in the handler (audit #467 TD-63,
// testCrashBreadcrumbConcurrentWriters).
std::mutex breadcrumbWriter;
std::atomic<wchar_t> breadcrumbText[256] = {L'(', L'n', L'o', L'n', L'e', L')', L'\0'};
std::atomic<unsigned long long> breadcrumbSequence{0};
static_assert(std::atomic<wchar_t>::is_always_lock_free);
static_assert(std::atomic<unsigned long long>::is_always_lock_free);
wchar_t dumpDirectory[MAX_PATH] = L"";
wchar_t productName[64] = L"Editor";
std::atomic<bool> handlingCrash(false);

const wchar_t* snapshotBreadcrumb(wchar_t (&snapshot)[256]) noexcept
{
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		const auto sequence = breadcrumbSequence.load();
		if (sequence & 1)
			continue;
		for (std::size_t i = 0; i < 255; ++i)
		{
			snapshot[i] = breadcrumbText[i].load();
			if (snapshot[i] == L'\0')
				break;
		}
		snapshot[255] = L'\0';
		if (breadcrumbSequence.load() == sequence)
			return snapshot;
	}
	return L"(breadcrumb changing)";
}

// %LOCALAPPDATA%\EqualizerAPO\logs\crash, created at install() time so the
// crash path itself only formats a file name. Audit #250 C4, maintainer
// decision 2026-08-09: dumps live under the same product log folder the
// diagnostics use (Logging::useUserFile writes EqualizerAPO\logs), in
// their own subfolder so operational logs and crash artifacts do not mix -
// the old EqualizerAPO-XT product root was a second product directory
// nothing else used.
void prepareDumpDirectory()
{
	wchar_t base[MAX_PATH];
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base)))
		return;
	swprintf_s(dumpDirectory, L"%s\\EqualizerAPO", base);
	CreateDirectoryW(dumpDirectory, nullptr);
	swprintf_s(dumpDirectory, L"%s\\EqualizerAPO\\logs", base);
	CreateDirectoryW(dumpDirectory, nullptr);
	swprintf_s(dumpDirectory, L"%s\\EqualizerAPO\\logs\\crash", base);
	CreateDirectoryW(dumpDirectory, nullptr);
}

void writeReport(EXCEPTION_POINTERS* pointers, const wchar_t* reason)
{
	// Re-entrancy / multi-thread guard: only the first crashing thread reports.
	bool expected = false;
	if (!handlingCrash.compare_exchange_strong(expected, true))
		return;
	if (dumpDirectory[0] == L'\0')
		return;

	const DWORD pid = GetCurrentProcessId();
	const DWORD tick = GetTickCount();

	wchar_t artifactName[128];
	CrashHandler::formatArtifactName(artifactName, productName, pid, tick, L"dmp");
	wchar_t dumpPath[MAX_PATH];
	swprintf_s(dumpPath, L"%s\\%s", dumpDirectory, artifactName);

	winutil::UniqueHandle dumpFile(
		CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (dumpFile)
	{
		MINIDUMP_EXCEPTION_INFORMATION info;
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = pointers;
		info.ClientPointers = FALSE;
		MiniDumpWriteDump(GetCurrentProcess(), pid, dumpFile.get(),
			static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
			pointers != nullptr ? &info : nullptr, nullptr, nullptr);
	}

	CrashHandler::formatArtifactName(artifactName, productName, pid, tick, L"txt");
	wchar_t textPath[MAX_PATH];
	swprintf_s(textPath, L"%s\\%s", dumpDirectory, artifactName);
	winutil::UniqueHandle textFile(
		CreateFileW(textPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (textFile)
	{
		wchar_t line[768];
		wchar_t breadcrumbSnapshot[256];
		CrashHandler::copyBreadcrumb(breadcrumbSnapshot);
		const DWORD code = (pointers != nullptr && pointers->ExceptionRecord != nullptr)
			? pointers->ExceptionRecord->ExceptionCode : 0;
		const void* address = (pointers != nullptr && pointers->ExceptionRecord != nullptr)
			? pointers->ExceptionRecord->ExceptionAddress : nullptr;
		int length = swprintf_s(line,
			L"EqualizerAPO-XT %s %d.%d.%d crash report\r\n"
			L"reason: %s\r\n"
			L"exception code: 0x%08lX at %p\r\n"
			L"last action: %s\r\n",
			productName, MAJOR, MINOR, REVISION, reason, code, address, breadcrumbSnapshot);
		if (length > 0)
		{
			DWORD written = 0;
			WriteFile(textFile.get(), line, length * sizeof(wchar_t), &written, nullptr);
		}
	}
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* pointers)
{
	writeReport(pointers, L"unhandled SEH exception");
	return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
	// Uncaught C++ exceptions land here in release builds. There are no
	// exception pointers; the dump still captures every thread's stack, and
	// the current exception (if any) is visible to the debugger via the CRT
	// state inside the dump.
	writeReport(nullptr, L"std::terminate (uncaught C++ exception)");
	TerminateProcess(GetCurrentProcess(), 0xC0DEDEAD);
}
}

namespace CrashHandler
{
void copyBreadcrumb(wchar_t (&snapshot)[256]) noexcept
{
	const wchar_t* value = snapshotBreadcrumb(snapshot);
	if (value != snapshot)
		wcsncpy_s(snapshot, value, _TRUNCATE);
}

void formatArtifactName(wchar_t (&name)[128], const wchar_t* product,
	unsigned long pid, unsigned long tick, const wchar_t* extension) noexcept
{
	swprintf_s(name, L"%s-%d.%d.%d-%lu-%lu.%s", product, MAJOR, MINOR, REVISION, pid, tick, extension);
}

void install(const wchar_t* product)
{
	wcsncpy_s(productName, product, _TRUNCATE);
	prepareDumpDirectory();
	SetUnhandledExceptionFilter(unhandledExceptionFilter);
	std::set_terminate(terminateHandler);
	// Error-mode dialogs would otherwise park a headless or kiosk-like machine
	// (PC-bang) on an invisible message box instead of exiting through the
	// handler.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

void setBreadcrumb(const std::wstring& text)
{
	std::lock_guard<std::mutex> lock(breadcrumbWriter);
	breadcrumbSequence.fetch_add(1);
	const std::size_t length = text.size() < 255 ? text.size() : 255;
	for (std::size_t i = 0; i < length; ++i)
		breadcrumbText[i].store(text[i]);
	breadcrumbText[length].store(L'\0');
	breadcrumbSequence.fetch_add(1);
}
}
