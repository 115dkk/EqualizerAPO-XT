/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "platform/windows/Win32Resource.h"

namespace eapo::ipc::beacon
{
	struct EngineVersion
	{
		unsigned major = 0, minor = 0, revision = 0;
		bool operator==(const EngineVersion&) const = default;
	};

	inline constexpr wchar_t kMappingName[] = L"Global\\EqualizerAPO-XT.EngineBeacon";

	// Permanent cross-version wire layout. Do not reorder or extend layout 1.
	struct Region
	{
		std::uint32_t magic; // 'EAPB', published last
		std::uint32_t layout; // 1
		std::uint32_t major, minor, revision;
		std::uint32_t processId;
	};

	void encode(Region& region, const EngineVersion& version, std::uint32_t processId) noexcept;
	std::optional<EngineVersion> decode(const void* bytes, std::size_t size) noexcept;
	EngineVersion builtVersion() noexcept;

	// Owns one publication independently of the process-wide slot, including in
	// tests using Local names. Failure never modifies an existing mapping.
	class Publication
	{
	public:
		Publication(const EngineVersion& version, const wchar_t* mappingName) noexcept;
		bool published() const noexcept { return error_ == ERROR_SUCCESS; }
		DWORD error() const noexcept { return error_; }

	private:
		DWORD create(const EngineVersion& version, const wchar_t* mappingName) noexcept;
		winutil::UniqueHandle mapping_;
		winutil::UniqueMappedView view_;
		DWORD error_ = ERROR_SUCCESS;
	};

	// The first result is retained until module unload, including failure.
	// On failure GetLastError() contains the original Win32 error on every call.
	bool publishOnce(const EngineVersion& version, const wchar_t* mappingName = kMappingName) noexcept;
	std::optional<EngineVersion> readPublished(const wchar_t* mappingName = kMappingName) noexcept;

	enum class EngineState { NotLoaded, Current, Stale };
	EngineState classify(const std::optional<EngineVersion>& running, const EngineVersion& installed) noexcept;
}
