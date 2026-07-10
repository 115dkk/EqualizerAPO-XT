/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <mutex>
#include "RegistryHelper.h"
#include "LogHelper.h"
#include "VSTPluginLibrary.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace std;

std::unordered_map<std::wstring, std::weak_ptr<VSTPluginLibrary>> VSTPluginLibrary::instanceMap;
std::wstring VSTPluginLibrary::defaultPluginPath;

// Guards the shared instanceMap. getInstance is called both from the GUI thread
// (VST card editors, filter GUIs) and from MainWindow's background AnalysisThread,
// which builds its own FilterEngine and resolves the same plugin libraries. The
// map is a plain unordered_map, so concurrent find/insert from those two threads
// corrupted it and crashed the Editor. A single static mutex serialises every
// lookup/insert.
static std::mutex& instanceMapMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::shared_ptr<VSTPluginLibrary> VSTPluginLibrary::getInstance(const wstring& libPath)
{
	lock_guard<mutex> lock(instanceMapMutex());

	shared_ptr<VSTPluginLibrary> ptr;

	auto it = instanceMap.find(libPath);
	if (it != instanceMap.end())
	{
		weak_ptr<VSTPluginLibrary> instance = it->second;
		ptr = instance.lock();
	}

	if (ptr == NULL)
	{
		ptr = shared_ptr<VSTPluginLibrary>(new VSTPluginLibrary(libPath));
		instanceMap[libPath] = ptr;
	}

	return ptr;
}

wstring VSTPluginLibrary::getDefaultPluginPath()
{
	// VSTPluginFilterFactory resolves relative library paths through this lazily
	// initialised static, and the factory runs on both the GUI thread and the
	// AnalysisThread, so the first-call assignment must be serialised too.
	static std::mutex defaultPathMutex;
	lock_guard<mutex> lock(defaultPathMutex);

	if (defaultPluginPath == L"")
	{
		try
		{
			wstring installPath = RegistryHelper::readValue(APP_REGPATH, L"InstallPath");
			defaultPluginPath = installPath + L"\\VSTPlugins";
		}
		catch (const RegistryException& e)
		{
			// No installed EqualizerAPO (dev tree, CI runner). The exception
			// must not escape: callers (parsing any VSTPlugin line with a
			// relative path, or building a VST row editor) do not expect it
			// and would terminate the Editor outright. Fall back to a
			// VSTPlugins folder beside the executable; installed systems have
			// the registry value and never take this path.
			LogFStatic(L"%s - falling back to the executable directory for VST plugins", e.getMessage().c_str());
			wchar_t modulePath[MAX_PATH];
			modulePath[0] = L'\0';
			GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
			wstring exePath = modulePath;
			size_t separator = exePath.find_last_of(L'\\');
			defaultPluginPath = (separator != wstring::npos ? exePath.substr(0, separator) : L".") + L"\\VSTPlugins";
		}
	}

	return defaultPluginPath;
}

std::wstring VSTPluginLibrary::getLibPath()
{
	return libPath;
}

std::wstring VSTPluginLibrary::getLoadPath()
{
	return loadPath;
}

bool VSTPluginLibrary::isVST3() const
{
	return vst3;
}

Steinberg::IPluginFactory* VSTPluginLibrary::getFactory() const
{
	return factory;
}

const Steinberg::PClassInfo& VSTPluginLibrary::getVST3ClassInfo() const
{
	return vst3ClassInfo;
}

bool VSTPluginLibrary::loadFunctions()
{
	if (vst3)
	{
		GetPluginFactory = (getPluginFactoryFunc)GetProcAddress(module, "GetPluginFactory");
		return GetPluginFactory != NULL;
	}

	VSTPluginMain = (vstPluginMain)GetProcAddress(module, "VSTPluginMain");
	return VSTPluginMain != NULL;
}

int VSTPluginLibrary::customInitialize()
{
	if (!vst3)
		return 0;

	factory = GetPluginFactory();
	if (factory == NULL)
		return FUNCTIONS_MISSING;

	for (Steinberg::int32 i = 0; i < factory->countClasses(); i++)
	{
		Steinberg::PClassInfo info;
		if (factory->getClassInfo(i, &info) == Steinberg::kResultOk
			&& strcmp(info.category, kVstAudioEffectClass) == 0)
		{
			vst3ClassInfo = info;
			return 0;
		}
	}

	return FUNCTIONS_MISSING;
}

VSTPluginLibrary::VSTPluginLibrary(const wstring& libPath)
	: libPath(libPath), loadPath(libPath)
{
	wchar_t extension[_MAX_EXT];
	_wsplitpath_s(libPath.c_str(), NULL, 0, NULL, 0, NULL, 0, extension, _MAX_EXT);
	vst3 = _wcsicmp(extension, L".vst3") == 0;
	if (vst3)
		loadPath = resolveVST3ModulePath(libPath);
}

wstring VSTPluginLibrary::resolveVST3ModulePath(const wstring& libPath)
{
	DWORD attributes = GetFileAttributesW(libPath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return libPath;

#if defined(_M_ARM64)
	const wchar_t* platformDir = L"arm64-win";
#elif defined(_WIN64)
	const wchar_t* platformDir = L"x86_64-win";
#else
	const wchar_t* platformDir = L"x86-win";
#endif

	wchar_t searchPath[MAX_PATH];
	wcscpy_s(searchPath, libPath.c_str());
	PathAppendW(searchPath, L"Contents");
	PathAppendW(searchPath, platformDir);
	PathAppendW(searchPath, L"*.vst3");

	WIN32_FIND_DATAW findData;
	HANDLE hFind = FindFirstFileW(searchPath, &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return libPath;

	wchar_t modulePath[MAX_PATH];
	wcscpy_s(modulePath, libPath.c_str());
	PathAppendW(modulePath, L"Contents");
	PathAppendW(modulePath, platformDir);
	PathAppendW(modulePath, findData.cFileName);
	FindClose(hFind);

	return modulePath;
}
