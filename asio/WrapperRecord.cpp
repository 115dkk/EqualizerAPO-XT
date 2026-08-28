/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "asio/WrapperRecord.h"

#include "services/registry/RegistryPaths.h"

namespace eapo::asio
{
	namespace
	{
		const wchar_t* const targetClsidValue = L"TargetClsid";
		const wchar_t* const targetNameValue = L"TargetName";
		const wchar_t* const processOutputValue = L"ProcessOutput";
		const wchar_t* const processInputValue = L"ProcessInput";
		const wchar_t* const modeValue = L"Mode";
		const wchar_t* const deadlineValue = L"DeadlineUs";
		const wchar_t* const readyTimeoutValue = L"ReadyTimeoutMs";
		const wchar_t* const lingerValue = L"LingerMs";
		const wchar_t* const deadlinePercentValue = L"DeadlinePercent";
		const wchar_t* const autoStartValue = L"AutoStart";
		const wchar_t* const register32Value = L"Register32";

		unsigned long readDwordOr(const IRegistry& registry, const std::wstring& key, const wchar_t* name, unsigned long fallback)
		{
			if (!registry.valueExists(key, name))
				return fallback;
			return registry.readDWORDValue(key, name);
		}
	}

	namespace WrapperRecords
	{
		std::wstring rootKey()
		{
			return std::wstring(APP_REGPATH) + L"\\ASIO";
		}

		std::wstring recordKey(const std::wstring& wrapperClsid)
		{
			return rootKey() + L"\\" + wrapperClsid;
		}

		bool read(const IRegistry& registry, const std::wstring& wrapperClsid, WrapperRecord& record)
		{
			const std::wstring key = recordKey(wrapperClsid);
			if (!registry.keyExists(key))
				return false;

			record.wrapperClsid = wrapperClsid;
			record.targetClsid = registry.readValue(key, targetClsidValue);
			record.targetName = registry.valueExists(key, targetNameValue) ? registry.readValue(key, targetNameValue) : L"";

			StreamOptions defaults;
			record.options = defaults;
			record.options.processOutput = readDwordOr(registry, key, processOutputValue, defaults.processOutput ? 1 : 0) != 0;
			record.options.processInput = readDwordOr(registry, key, processInputValue, defaults.processInput ? 1 : 0) != 0;
			record.options.mode = readDwordOr(registry, key, modeValue, defaults.mode == Mode::Pipelined ? 1 : 0) == 1 ? Mode::Pipelined : Mode::Sync;
			record.options.deadlineUs = readDwordOr(registry, key, deadlineValue, defaults.deadlineUs);
			record.options.readyTimeoutMs = readDwordOr(registry, key, readyTimeoutValue, defaults.readyTimeoutMs);
			record.options.lingerMs = readDwordOr(registry, key, lingerValue, defaults.lingerMs);
			record.options.deadlinePercent = readDwordOr(registry, key, deadlinePercentValue, defaults.deadlinePercent);
			record.autoStart = readDwordOr(registry, key, autoStartValue, 0) != 0;
			record.register32 = readDwordOr(registry, key, register32Value, 0) != 0;
			return true;
		}

		void write(IRegistry& registry, const WrapperRecord& record)
		{
			const std::wstring key = recordKey(record.wrapperClsid);
			registry.createKey(key);
			registry.writeValue(key, targetClsidValue, record.targetClsid);
			registry.writeValue(key, targetNameValue, record.targetName);
			registry.writeDWORDValue(key, processOutputValue, record.options.processOutput ? 1 : 0);
			registry.writeDWORDValue(key, processInputValue, record.options.processInput ? 1 : 0);
			registry.writeDWORDValue(key, modeValue, record.options.mode == Mode::Pipelined ? 1 : 0);
			registry.writeDWORDValue(key, deadlineValue, record.options.deadlineUs);
			registry.writeDWORDValue(key, readyTimeoutValue, record.options.readyTimeoutMs);
			registry.writeDWORDValue(key, lingerValue, record.options.lingerMs);
			registry.writeDWORDValue(key, deadlinePercentValue, record.options.deadlinePercent);
			registry.writeDWORDValue(key, autoStartValue, record.autoStart ? 1 : 0);
			registry.writeDWORDValue(key, register32Value, record.register32 ? 1 : 0);
		}

		void remove(IRegistry& registry, const std::wstring& wrapperClsid)
		{
			const std::wstring key = recordKey(wrapperClsid);
			if (registry.keyExists(key))
				registry.deleteKey(key);
		}
	}
}
