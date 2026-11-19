/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	When a test executable dies from an unhandled structured exception, the
	only trace used to be its exit code. This filter writes where it died to
	stderr first: exception code, faulting address, the module and offset of
	the faulting instruction and of the callers above it, and how much of the
	thread's stack was left. An intermittent crash in CI then leaves enough to
	find the function without reproducing it.

	It runs on the faulting thread, which may have almost no stack left, so it
	uses static buffers and no CRT formatting.
*/

#pragma once

#include <windows.h>

namespace test
{
namespace crash_report
{
inline char* appendText(char* out, char* end, const char* text)
{
	while (*text != '\0' && out < end)
		*out++ = *text++;
	return out;
}

inline char* appendHex(char* out, char* end, unsigned long long value)
{
	static const char digits[] = "0123456789abcdef";
	char buffer[16];
	int count = 0;
	do
	{
		buffer[count++] = digits[value & 0xf];
		value >>= 4;
	}
	while (value != 0);
	out = appendText(out, end, "0x");
	while (count > 0 && out < end)
		*out++ = buffer[--count];
	return out;
}

inline char* appendAddress(char* out, char* end, const void* address)
{
	static char modulePath[MAX_PATH];
	HMODULE module = nullptr;
	if (address != nullptr && GetModuleHandleExA(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCSTR>(address), &module) && module != nullptr)
	{
		const DWORD length = GetModuleFileNameA(module, modulePath, MAX_PATH);
		const char* name = modulePath;
		for (DWORD i = 0; i < length; i++)
		{
			if (modulePath[i] == '\\' || modulePath[i] == '/')
				name = modulePath + i + 1;
		}
		out = appendText(out, end, length != 0 ? name : "?");
		out = appendText(out, end, "+");
		return appendHex(out, end, static_cast<unsigned long long>(
			static_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
	}
	return appendHex(out, end, reinterpret_cast<unsigned long long>(address));
}

inline LONG WINAPI report(EXCEPTION_POINTERS* info)
{
	static char text[4096];
	static void* frames[32];
	char* out = text;
	char* end = text + sizeof(text) - 2;
	const EXCEPTION_RECORD* record = info->ExceptionRecord;

	out = appendText(out, end, "\nUNHANDLED EXCEPTION ");
	out = appendHex(out, end, record->ExceptionCode);
	out = appendText(out, end, " at ");
	out = appendAddress(out, end, record->ExceptionAddress);
	if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
		&& record->NumberParameters >= 2)
	{
		out = appendText(out, end, record->ExceptionInformation[0] == 0 ? ", reading "
			: record->ExceptionInformation[0] == 1 ? ", writing " : ", executing ");
		out = appendHex(out, end, record->ExceptionInformation[1]);
	}

	ULONG_PTR stackLow = 0, stackHigh = 0;
	GetCurrentThreadStackLimits(&stackLow, &stackHigh);
#if defined(_M_X64)
	const ULONG_PTR stackPointer = info->ContextRecord->Rsp;
#elif defined(_M_ARM64)
	const ULONG_PTR stackPointer = info->ContextRecord->Sp;
#else
	const ULONG_PTR stackPointer = info->ContextRecord->Esp;
#endif
	out = appendText(out, end, "\n  thread ");
	out = appendHex(out, end, GetCurrentThreadId());
	out = appendText(out, end, ", stack pointer ");
	out = appendHex(out, end, stackPointer);
	out = appendText(out, end, ", stack ");
	out = appendHex(out, end, stackLow);
	out = appendText(out, end, "..");
	out = appendHex(out, end, stackHigh);
	out = appendText(out, end, " (");
	out = appendHex(out, end, stackPointer - stackLow);
	out = appendText(out, end, " bytes above the lowest address)");

	// The frames of the filter itself come first; the faulting frames follow
	// the exception dispatcher's.
	const USHORT count = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
	for (USHORT i = 0; i < count; i++)
	{
		out = appendText(out, end, "\n  [");
		out = appendHex(out, end, i);
		out = appendText(out, end, "] ");
		out = appendAddress(out, end, frames[i]);
	}
	out = appendText(out, end, "\n");

	DWORD written = 0;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(out - text), &written, nullptr);
	return EXCEPTION_EXECUTE_HANDLER;
}

inline void install()
{
#ifndef __SANITIZE_ADDRESS__
	// AddressSanitizer reports crashes itself and owns this filter.
	// No SetThreadStackGuarantee here: it would change how the stack-overflow
	// tests behave. An overflow nothing contained may therefore go unreported.
	SetUnhandledExceptionFilter(report);
#endif
}
}
}
