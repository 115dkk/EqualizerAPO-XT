/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/windows/Win32Resource.h"

namespace eapo::ipc::send
{
	class SendRingWin32
	{
	public:
		SendRingWin32(const std::wstring& mappingObjectName, const std::wstring& eventObjectName);

		SendRingWin32(const SendRingWin32&) = delete;
		SendRingWin32& operator=(const SendRingWin32&) = delete;
		SendRingWin32(SendRingWin32&&) noexcept = default;
		SendRingWin32& operator=(SendRingWin32&&) noexcept = default;

		void* region() const noexcept;
		HANDLE readyEvent() const noexcept;
		void touchAllPages() noexcept;
		bool signal() const noexcept;

	private:
		winutil::UniqueHandle mapping_;
		winutil::UniqueMappedView view_;
		winutil::UniqueHandle event_;
	};
}
