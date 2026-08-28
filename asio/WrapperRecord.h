/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The registry record behind one wrapper entry. Every target driver the
	user enables gets its own wrapper CLSID; the record under
	HKLM\SOFTWARE\EqualizerAPO\ASIO\{wrapperClsid} tells DllGetClassObject
	which target to load and how to run the stream. Read through the registry
	port so a fake registry can pin the vocabulary.
*/

#pragma once

#include <string>

#include "asio/StreamProcessor.h"
#include "services/registry/IRegistry.h"

namespace eapo::asio
{
	struct WrapperRecord
	{
		std::wstring wrapperClsid;   // {...}
		std::wstring targetClsid;    // {...}
		std::wstring targetName;     // the target's HKLM\SOFTWARE\ASIO subkey name
		StreamOptions options;
		bool autoStart = false;      // start the engine host at boot (one Run value); off by default
		bool register32 = false;     // also register the entry for 32-bit hosts; off by default
	};

	namespace WrapperRecords
	{
		// HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO\ASIO
		std::wstring rootKey();
		std::wstring recordKey(const std::wstring& wrapperClsid);

		// False when no record exists for the CLSID; throws RegistryError on
		// a record that exists but cannot be read.
		bool read(const IRegistry& registry, const std::wstring& wrapperClsid, WrapperRecord& record);

		// Creates the key and writes every value. Options that carry their
		// defaults are written too, so the record is self-describing.
		void write(IRegistry& registry, const WrapperRecord& record);

		// Deletes the record; a missing record is not an error.
		void remove(IRegistry& registry, const std::wstring& wrapperClsid);
	}
}
