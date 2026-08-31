/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "asio/AsioRegistration.h"

#include "platform/windows/GuidText.h"
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
			if (FAILED(CLSIDFromString(targetClsid.c_str(), &guid)))
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
				if (!target.clsid.empty())
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
			const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
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
			const std::wstring wrapperClsid = wrapperClsidFor(target.clsid);
			const std::wstring entryName = entryNameFor(target.name);
			for (int view = 0; view < 2; view++)
			{
				const bool wow = view == 1;
				deleteKeyIfPresent(registry, asioRoot(wow) + L"\\" + entryName);
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
	}
}
