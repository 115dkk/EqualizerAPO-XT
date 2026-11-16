/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <thread>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winutil
{
// MSVC's native thread handle becomes signaled after DLL_THREAD_DETACH too.
// A condition signaled by the worker before returning would still allow join()
// to hang on a plug-in's loader lock (audit #467 TD-58).
inline bool joinThreadFor(std::thread& thread, unsigned long timeoutMs)
{
	if (!thread.joinable())
		return true;
	if (WaitForSingleObject(thread.native_handle(), timeoutMs) != WAIT_OBJECT_0)
		return false;
	thread.join();
	return true;
}
}
