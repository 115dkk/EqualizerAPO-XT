/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "runtime/ipc/EngineBeacon.h"

#include <atomic>
#include <cstring>
#include <sddl.h>

#include "version.h"

namespace eapo::ipc::beacon
{
	namespace
	{
		constexpr std::uint32_t kMagic = 0x42504145; // EAPB on Windows (little endian)
		constexpr std::uint32_t kLayout = 1;
		// Section rights, not file rights: 0x5 = SECTION_QUERY | SECTION_MAP_READ.
		// FILE_MAP_COPY is not a DACL right. SYSTEM, LOCAL SERVICE and admins
		// receive full access; other authenticated users cannot write or change ACLs.
		constexpr wchar_t kSecurity[] = L"D:P(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;0x5;;;AU)";
		static_assert((SECTION_QUERY | SECTION_MAP_READ) == 0x5);
		static_assert(sizeof(Region) == 24 && offsetof(Region, processId) == 20);
		static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
	}

	void encode(Region& region, const EngineVersion& version, std::uint32_t processId) noexcept
	{
		region = {kMagic, kLayout, version.major, version.minor, version.revision, processId};
	}

	std::optional<EngineVersion> decode(const void* bytes, std::size_t size) noexcept
	{
		if (bytes == nullptr || size < sizeof(Region))
			return std::nullopt;
		Region region;
		std::memcpy(&region, bytes, sizeof(region));
		if (region.magic != kMagic || region.layout != kLayout)
			return std::nullopt;
		return EngineVersion{region.major, region.minor, region.revision};
	}

	EngineVersion builtVersion() noexcept
	{
		return {MAJOR, MINOR, REVISION};
	}

	Publication::Publication(const EngineVersion& version, const wchar_t* mappingName) noexcept
	{
		error_ = create(version, mappingName);
		if (error_ != ERROR_SUCCESS)
		{
			view_.reset();
			mapping_.reset();
		}
		SetLastError(error_);
	}

	DWORD Publication::create(const EngineVersion& version, const wchar_t* mappingName) noexcept
	{
		if (mappingName == nullptr || *mappingName == L'\0')
			return ERROR_INVALID_NAME;
		winutil::UniqueLocalPtr<void> descriptor;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSecurity, SDDL_REVISION_1, descriptor.put(), nullptr))
			return GetLastError();
		SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor.get(), FALSE};
		mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(Region), mappingName));
		const DWORD error = GetLastError();
		if (!mapping_ || error == ERROR_ALREADY_EXISTS)
			return error;
		view_.reset(MapViewOfFile(mapping_.get(), FILE_MAP_WRITE, 0, 0, sizeof(Region)));
		if (!view_)
			return GetLastError();
		Region encoded;
		encode(encoded, version, GetCurrentProcessId());
		auto* region = static_cast<Region*>(view_.get());
		// The name becomes visible before initialization finishes. Readers reject
		// zero magic until this release store publishes the immutable payload.
		std::memcpy(static_cast<unsigned char*>(view_.get()) + sizeof(region->magic),
			reinterpret_cast<const unsigned char*>(&encoded) + sizeof(encoded.magic), sizeof(Region) - sizeof(encoded.magic));
		std::atomic_ref<std::uint32_t>(region->magic).store(kMagic, std::memory_order_release);
		return ERROR_SUCCESS;
	}

	bool publishOnce(const EngineVersion& version, const wchar_t* mappingName) noexcept
	{
		static const Publication publication(version, mappingName);
		SetLastError(publication.error());
		return publication.published();
	}

	std::optional<EngineVersion> readPublished(const wchar_t* mappingName) noexcept
	{
		winutil::UniqueHandle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName));
		if (!mapping)
			return std::nullopt;
		winutil::UniqueMappedView view(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, sizeof(Region)));
		if (!view)
			return std::nullopt;
		const auto* region = static_cast<const Region*>(view.get());
		// A load-only atomic_ref works on the read-only view, unlike an interlocked
		// compare/exchange, which would require write access even for a read.
		if (std::atomic_ref<const std::uint32_t>(region->magic).load(std::memory_order_acquire) != kMagic)
			return std::nullopt;
		return decode(region, sizeof(Region));
	}

	EngineState classify(const std::optional<EngineVersion>& running, const EngineVersion& installed) noexcept
	{
		if (!running)
			return EngineState::NotLoaded;
		return *running == installed ? EngineState::Current : EngineState::Stale;
	}
}
