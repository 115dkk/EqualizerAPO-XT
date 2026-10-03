/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "runtime/ipc/SendRingWin32.h"

#include <cstddef>
#include <system_error>

#include "runtime/ipc/SendRing.h"

namespace eapo::ipc::send
{
	SendRingWin32::SendRingWin32(const std::wstring& mappingObjectName, const std::wstring& eventObjectName)
		: mapping_(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
			static_cast<DWORD>(regionBytes), mappingObjectName.c_str()))
	{
		if (!mapping_)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateFileMappingW");

		view_.reset(MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, regionBytes));
		if (!view_)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "MapViewOfFile");

		event_.reset(CreateEventW(nullptr, TRUE, FALSE, eventObjectName.c_str()));
		if (!event_)
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateEventW");
	}

	void* SendRingWin32::region() const noexcept
	{
		return view_.get();
	}

	HANDLE SendRingWin32::readyEvent() const noexcept
	{
		return event_.get();
	}

	void SendRingWin32::touchAllPages() noexcept
	{
		auto* bytes = static_cast<volatile unsigned char*>(view_.get());
		if (bytes == nullptr)
			return;
		const unsigned char firstValue = bytes[headerBytes];
		// cppcheck-suppress redundantAssignment ; the volatile write deliberately preserves live ring data while faulting in the page
		bytes[headerBytes] = firstValue;
		for (size_t offset = (headerBytes + 4095) & ~size_t(4095);
			offset < regionBytes; offset += 4096)
		{
			const unsigned char value = bytes[offset];
			// cppcheck-suppress redundantAssignment ; the volatile write deliberately preserves live ring data while faulting in the page
			bytes[offset] = value;
		}
	}

	bool SendRingWin32::signal() const noexcept
	{
		return SetEvent(event_.get()) != FALSE;
	}
}
