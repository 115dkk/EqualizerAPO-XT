/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "asio/AsioRegistration.h"

#include "asio/WrapperRecord.h"
#include "platform/windows/GuidText.h"
#include "runtime/errors/WideError.h"
#include "services/registry/ClsidRegistration.h"

namespace eapo::asio
{
	namespace
	{
		const wchar_t* const suffix = L" (EQ APO XT)";
		const wchar_t* const clsidValue = L"CLSID";
		const wchar_t* const descriptionValue = L"Description";
		const wchar_t* const className = L"EQ APO XT driver wrapper";
		const wchar_t* const runKey = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
		const wchar_t* const runValueName = L"EqualizerAPOHost";

		void deleteKeyIfPresent(IRegistry& registry, const std::wstring& key)
		{
			if (registry.keyExists(key))
				registry.deleteKey(key);
		}

		// A braced GUID and nothing else. CLSIDFromString would also accept a
		// ProgID and look it up; IIDFromString takes only the GUID form.
		bool parseGuid(const std::wstring& text, GUID& guid)
		{
			return !text.empty() && SUCCEEDED(IIDFromString(text.c_str(), &guid));
		}

		// The wrapper CLSID of a target, refused when the target's CLSID is
		// not a GUID: the empty result would name the CLSID root itself, and
		// registerWrapper would write the class values there (audit #348
		// TD-45).
		std::wstring requireWrapperClsid(const AsioTarget& target)
		{
			const std::wstring wrapperClsid = AsioRegistration::wrapperClsidFor(target.clsid);
			if (wrapperClsid.empty())
				throw WideError(L"The ASIO target " + target.name + L" has a CLSID that is not a GUID: " + target.clsid);
			return wrapperClsid;
		}
	}

	namespace AsioRegistration
	{
		std::wstring entrySuffix()
		{
			return suffix;
		}

		std::wstring asioRoot(bool wow6432)
		{
			return wow6432 ? L"HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\ASIO" : L"HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO";
		}

		std::wstring classesClsidRoot(bool wow6432)
		{
			return wow6432 ? L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\WOW6432Node\\CLSID"
				: L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID";
		}

		std::wstring entryNameFor(const std::wstring& targetName)
		{
			return targetName + suffix;
		}

		bool isWrapperEntry(const std::wstring& entryName)
		{
			const std::wstring tail(suffix);
			return entryName.ends_with(tail);
		}

		std::wstring wrapperClsidFor(const std::wstring& targetClsid)
		{
			GUID guid = {};
			if (!parseGuid(targetClsid, guid))
				return std::wstring();
			// Flip a fixed pattern into the target's id and stamp it as a
			// random-style (version 4) GUID, so the derived id can never equal
			// another driver's registered id by accident.
			guid.Data1 ^= 0x5845514fu;
			guid.Data2 ^= 0x4150u;
			guid.Data3 = static_cast<unsigned short>((guid.Data3 & 0x0fffu) | 0x4000u);
			guid.Data4[0] = static_cast<unsigned char>((guid.Data4[0] & 0x3fu) | 0x80u);
			guid.Data4[7] ^= 0x58u;
			return winutil::guidToString(guid);
		}

		AsioTarget endpointTarget(const std::wstring& endpointGuid, const std::wstring& connectionName, const std::wstring& deviceName)
		{
			AsioTarget target;
			std::wstring name = deviceName.empty() ? connectionName : (connectionName.empty() ? deviceName : deviceName + L" - " + connectionName);
			for (wchar_t& c : name)
				if (c == L'\\')
					c = L'/';
			target.name = name;
			target.clsid = endpointGuid;
			target.description = name;
			return target;
		}

		std::vector<AsioTarget> enumerateTargets(const IRegistry& registry)
		{
			std::vector<AsioTarget> targets;
			const std::wstring root = asioRoot(false);
			if (!registry.keyExists(root))
				return targets;
			for (const std::wstring& name : registry.enumSubKeys(root))
			{
				if (isWrapperEntry(name))
					continue;
				const std::wstring key = root + L"\\" + name;
				if (!registry.valueExists(key, clsidValue))
					continue;
				AsioTarget target;
				target.name = name;
				target.clsid = registry.readValue(key, clsidValue);
				target.description = registry.valueExists(key, descriptionValue) ? registry.readValue(key, descriptionValue) : name;
				// A driver whose CLSID is not a GUID cannot be loaded, and no
				// wrapper CLSID derives from it; it is not offered.
				GUID guid = {};
				if (parseGuid(target.clsid, guid))
					targets.push_back(std::move(target));
			}
			return targets;
		}

		bool wrapperRegistered(const IRegistry& registry, const AsioTarget& target)
		{
			return registry.keyExists(asioRoot(false) + L"\\" + entryNameFor(target.name));
		}

		void registerWrapper(IRegistry& registry, const AsioTarget& target,
			const std::wstring& dll64Path, const std::wstring& dll32Path)
		{
			const std::wstring wrapperClsid = requireWrapperClsid(target);
			const std::wstring entryName = entryNameFor(target.name);
			for (int view = 0; view < 2; view++)
			{
				const bool wow = view == 1;
				const std::wstring& dll = wow ? dll32Path : dll64Path;
				if (dll.empty())
					continue;
				const std::wstring entryKey = asioRoot(wow) + L"\\" + entryName;
				registry.createKey(entryKey);
				registry.writeValue(entryKey, clsidValue, wrapperClsid);
				registry.writeValue(entryKey, descriptionValue, target.description + suffix);
				ClsidRegistration::registerClsidTreeAt(registry, classesClsidRoot(wow), wrapperClsid, className, dll);
			}
		}

		void unregisterWrapper(IRegistry& registry, const AsioTarget& target)
		{
			// registerWrapper refuses a target whose CLSID is not a GUID, so
			// nothing was ever written for one; an empty wrapper CLSID must
			// not reach the key paths below, where it would name the CLSID
			// root.
			const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
			if (wrapperClsid.empty())
				return;
			const std::wstring entryName = entryNameFor(target.name);
			for (int view = 0; view < 2; view++)
			{
				const bool wow = view == 1;
				// Every entry that points at this wrapper, whatever it is called.
				// The entry is named after the target, and an endpoint target's
				// name is the device's friendly name at install time: after the
				// user (or a driver update) renamed the device, the name derived
				// here no longer matched, and the old entry stayed in every DAW's
				// list pointing at a CLSID that was unregistered below (audit
				// #348 TD-08).
				const std::wstring root = asioRoot(wow);
				if (registry.keyExists(root))
				{
					for (const std::wstring& name : registry.enumSubKeys(root))
					{
						const std::wstring key = root + L"\\" + name;
						if (registry.valueExists(key, clsidValue)
							&& _wcsicmp(registry.readValue(key, clsidValue).c_str(), wrapperClsid.c_str()) == 0)
							registry.deleteKey(key);
					}
				}
				deleteKeyIfPresent(registry, root + L"\\" + entryName);
				const std::wstring classKey = classesClsidRoot(wow) + L"\\" + wrapperClsid;
				deleteKeyIfPresent(registry, classKey + L"\\InprocServer32");
				deleteKeyIfPresent(registry, classKey);
			}
		}

		std::wstring autoStartKey()
		{
			return runKey;
		}

		std::wstring autoStartValueName()
		{
			return runValueName;
		}

		bool autoStartRegistered(const IRegistry& registry)
		{
			return registry.keyExists(runKey) && registry.valueExists(runKey, runValueName);
		}

		void setAutoStart(IRegistry& registry, const std::wstring& hostExePath, bool wanted)
		{
			if (wanted)
			{
				if (!registry.keyExists(runKey))
					registry.createKey(runKey);
				registry.writeValue(runKey, runValueName, L"\"" + hostExePath + L"\" --resident");
			}
			else if (autoStartRegistered(registry))
			{
				registry.deleteValue(runKey, runValueName);
			}
		}

		void refreshAutoStart(IRegistry& registry, const std::wstring& installDirectory)
		{
			const bool wanted = WrapperRecords::autoStartWanted(registry);
			if (wanted && installDirectory.empty())
				return;
			setAutoStart(registry, installDirectory + L"\\EqualizerAPOHost.exe", wanted);
		}

		std::wstring wrapperDllPath(const std::wstring& installDirectory)
		{
			return installDirectory + L"\\EqualizerAPOAsio.dll";
		}

		std::wstring wrapper32DllPath(const std::wstring& installDirectory)
		{
			return installDirectory + L"\\x86\\EqualizerAPOAsio.dll";
		}

		bool wrapper32Shipped(const std::wstring& installDirectory)
		{
			if (installDirectory.empty())
				return false;
			const DWORD attributes = GetFileAttributesW(wrapper32DllPath(installDirectory).c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}
	}
}
