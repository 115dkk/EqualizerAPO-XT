/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The registry vocabulary that makes a target driver usable through the
	wrapper: one entry per target under HKLM\SOFTWARE\ASIO (the list every
	DAW reads), its CLSID's InprocServer32 pointing at the wrapper DLL, and
	the same pair in the WOW6432Node views for 32-bit hosts. The wrapper
	CLSID is derived from the target's, so nothing else has to remember the
	pairing. Everything goes through the registry port; the registry's
	64-bit view sees the WOW6432Node keys as ordinary keys, which is why no
	second port is needed.

	Trademark note: the entry name is the target's own name with an
	"(EQ APO XT)" suffix; the wrapper never puts "ASIO" into a product name.
*/

#pragma once

#include <string>
#include <vector>

#include "services/registry/IRegistry.h"

namespace eapo::asio
{
	struct AsioTarget
	{
		std::wstring name;          // the HKLM\SOFTWARE\ASIO subkey
		std::wstring clsid;         // {...}
		std::wstring description;   // the Description value, or the name
	};

	namespace AsioRegistration
	{
		std::wstring entrySuffix();
		std::wstring asioRoot(bool wow6432);
		std::wstring classesClsidRoot(bool wow6432);

		// The wrapper entry's name for a target, and whether a name is one.
		std::wstring entryNameFor(const std::wstring& targetName);
		bool isWrapperEntry(const std::wstring& entryName);

		// A CLSID for the wrapper entry, derived from the target's CLSID so
		// the same target always maps to the same wrapper.
		std::wstring wrapperClsidFor(const std::wstring& targetClsid);

		// A Windows audio endpoint as a target: the entry is named after the
		// device and the endpoint ("TOPPING USB DAC - Speakers"), its CLSID
		// is the endpoint GUID, so the wrapper CLSID derives from it the same
		// way it does for a driver. Backslashes cannot name a registry key
		// and become slashes.
		AsioTarget endpointTarget(const std::wstring& endpointGuid, const std::wstring& connectionName, const std::wstring& deviceName);
		// Every target driver registered in the 64-bit view, without the
		// wrapper's own entries. A subkey without a CLSID is skipped.
		std::vector<AsioTarget> enumerateTargets(const IRegistry& registry);

		bool wrapperRegistered(const IRegistry& registry, const AsioTarget& target);

		// Writes the entry and the class tree in the 64-bit view, and again in
		// the WOW6432Node view when a 32-bit DLL path is given. Idempotent.
		void registerWrapper(IRegistry& registry, const AsioTarget& target,
			const std::wstring& dll64Path, const std::wstring& dll32Path);

		// Removes what registerWrapper wrote, in both views, ignoring what is
		// already absent.
		void unregisterWrapper(IRegistry& registry, const AsioTarget& target);

		// The engine host at boot: one Run value for the machine, present
		// while any target asks for it. The host is one per session, so one
		// value serves every target; it starts the host resident.
		std::wstring autoStartKey();
		std::wstring autoStartValueName();
		bool autoStartRegistered(const IRegistry& registry);
		void setAutoStart(IRegistry& registry, const std::wstring& hostExePath, bool wanted);
	}
}
