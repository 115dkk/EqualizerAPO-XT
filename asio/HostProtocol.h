/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the wrapper and the engine host agree on besides the ring: the
	names of the kernel objects and the one message exchanged over the
	control pipe when a stream opens. Fixed-width, no pointers; a 32-bit
	wrapper and a 64-bit host read the same bytes.

	Naming, all in the per-session Local\ namespace:
	  pipe     \\.\pipe\<endpoint>
	  owner    Local\<endpoint>.owner            (mutex: one host per endpoint)
	  ring     Local\<endpoint>.ring.<pid>.<serial>
	  events   <ring>.work0 .work1 .done0 .done1 .ready  (RingEvents below)
	The default endpoint is EAPO.ASIO.<session id>; the probe pins its own so
	a CI run never meets a user's host.
*/

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime/ipc/StreamRing.h"

namespace eapo::asio
{
	constexpr uint32_t hostProtocolVersion = 1;

	struct HostOpenRequest
	{
		uint32_t version = hostProtocolVersion;
		uint32_t producerPid = 0;
		uint32_t ringBytes = 0;
		uint32_t lingerMs = 0;
		wchar_t ringName[128] = {};     // the mapping; the events derive from it
		wchar_t configPath[260] = {};   // empty = registry ConfigPath + watcher
	};

	enum class HostOpenStatus : uint32_t
	{
		Accepted = 0,
		BadVersion = 1,
		BadRing = 2,
		ShuttingDown = 3
	};

	struct HostOpenReply
	{
		uint32_t status = 0;            // HostOpenStatus
		uint32_t hostPid = 0;
	};

	static_assert(std::is_standard_layout_v<HostOpenRequest> && std::is_trivially_copyable_v<HostOpenRequest>,
		"HostOpenRequest must remain a plain wire type");
	static_assert(sizeof(HostOpenRequest) == 16 + 256 + 520, "HostOpenRequest must stay fixed-width");
	static_assert(std::is_standard_layout_v<HostOpenReply> && std::is_trivially_copyable_v<HostOpenReply>,
		"HostOpenReply must remain a plain wire type");
	static_assert(sizeof(HostOpenReply) == 8, "HostOpenReply must stay fixed-width");

	namespace HostNames
	{
		inline std::wstring defaultEndpoint()
		{
			DWORD session = 0;
			ProcessIdToSessionId(GetCurrentProcessId(), &session);
			return L"EAPO.ASIO." + std::to_wstring(session);
		}

		inline std::wstring pipe(const std::wstring& endpoint)
		{
			return L"\\\\.\\pipe\\" + endpoint;
		}

		inline std::wstring owner(const std::wstring& endpoint)
		{
			return L"Local\\" + endpoint + L".owner";
		}

		inline std::wstring ring(const std::wstring& endpoint, uint32_t pid, uint32_t serial)
		{
			return L"Local\\" + endpoint + L".ring." + std::to_wstring(pid) + L"." + std::to_wstring(serial);
		}

		inline std::wstring event(const std::wstring& ring, const wchar_t* suffix)
		{
			return ring + L"." + suffix;
		}
	}

	// The five events of one ring, in the one order every side keeps them
	// in: the work event of each lane (the producer sets it after publish),
	// the done event of each lane (the consumer sets it after release), and
	// ready (Ready or Fault written). Only ready is manual-reset: the
	// producer may look at it more than once, while a work or done event is
	// consumed by the one wait it wakes. The wrapper creates them named, the
	// engine host opens them by name, the thread host creates them unnamed.
	namespace RingEvents
	{
		constexpr unsigned count = 5;

		struct Entry
		{
			const wchar_t* suffix;
			bool manualReset;
		};

		inline constexpr Entry table[count] = {
			{L"work0", false},
			{L"work1", false},
			{L"done0", false},
			{L"done1", false},
			{L"ready", true}
		};

		// Places the five handles, in table order, into a RingSync with
		// `peer` as the other side's liveness handle.
		inline eapo::ipc::RingSync toSync(const HANDLE (&events)[count], HANDLE peer) noexcept
		{
			eapo::ipc::RingSync sync;
			sync.work[0] = events[0];
			sync.work[1] = events[1];
			sync.done[0] = events[2];
			sync.done[1] = events[3];
			sync.ready = events[4];
			sync.peer = peer;
			return sync;
		}
	}
}
