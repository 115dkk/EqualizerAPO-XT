#include "CrashHandler.h"

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include "version.h"

namespace
{
// Fixed-size storage only: the handler runs in a crashed process, so it must
// not allocate. The breadcrumb is written by the UI thread and read by the
// handler; a torn read of a short text buffer is acceptable.
wchar_t breadcrumb[256] = L"(none)";
wchar_t dumpDirectory[MAX_PATH] = L"";
std::atomic<bool> handlingCrash(false);

// %LOCALAPPDATA%\EqualizerAPO-XT\crashdumps, created at install() time so the
// crash path itself only formats a file name.
void prepareDumpDirectory()
{
	wchar_t base[MAX_PATH];
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base)))
		return;
	swprintf_s(dumpDirectory, L"%s\\EqualizerAPO-XT", base);
	CreateDirectoryW(dumpDirectory, nullptr);
	swprintf_s(dumpDirectory, L"%s\\EqualizerAPO-XT\\crashdumps", base);
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

	wchar_t dumpPath[MAX_PATH];
	swprintf_s(dumpPath, L"%s\\Editor-%d.%d.%d-%lu-%lu.dmp", dumpDirectory,
		MAJOR, MINOR, REVISION, pid, tick);

	HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (dumpFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION info;
		info.ThreadId = GetCurrentThreadId();
		info.ExceptionPointers = pointers;
		info.ClientPointers = FALSE;
		MiniDumpWriteDump(GetCurrentProcess(), pid, dumpFile,
			static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
			pointers != nullptr ? &info : nullptr, nullptr, nullptr);
		CloseHandle(dumpFile);
	}

	wchar_t textPath[MAX_PATH];
	swprintf_s(textPath, L"%s\\Editor-%d.%d.%d-%lu-%lu.txt", dumpDirectory,
		MAJOR, MINOR, REVISION, pid, tick);
	HANDLE textFile = CreateFileW(textPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (textFile != INVALID_HANDLE_VALUE)
	{
		wchar_t line[768];
		const DWORD code = (pointers != nullptr && pointers->ExceptionRecord != nullptr)
			? pointers->ExceptionRecord->ExceptionCode : 0;
		const void* address = (pointers != nullptr && pointers->ExceptionRecord != nullptr)
			? pointers->ExceptionRecord->ExceptionAddress : nullptr;
		int length = swprintf_s(line,
			L"EqualizerAPO-XT Editor %d.%d.%d crash report\r\n"
			L"reason: %s\r\n"
			L"exception code: 0x%08lX at %p\r\n"
			L"last action: %s\r\n",
			MAJOR, MINOR, REVISION, reason, code, address, breadcrumb);
		if (length > 0)
		{
			DWORD written = 0;
			WriteFile(textFile, line, length * sizeof(wchar_t), &written, nullptr);
		}
		CloseHandle(textFile);
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
void install()
{
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
	wcsncpy_s(breadcrumb, text.c_str(), _TRUNCATE);
}
}
