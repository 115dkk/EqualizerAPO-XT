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
#include "services/registry/RegistryPaths.h"
#include <mutex>
#include "services/registry/WindowsRegistry.h"
#include "services/logging/Logging.h"
#include "VSTPluginLibrary.h"
#include "VST3HostObjects.h"
#include "platform/windows/Win32Resource.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

using namespace std;

namespace
{
// The host context a VST3 module sees before any class is instantiated
// (IPluginFactory3::setHostContext). Some plug-ins consult it for the host
// name or ask it to manufacture IMessage/IAttributeList objects during
// factory-level setup. Ported from the ripDZL fork's VST3 compatibility
// work (github.com/ripDZL/EqualizerAPO-XT/pull/1).
class VST3FactoryHostContext : public Steinberg::Vst::IHostApplication
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IHostApplication)
		QUERY_INTERFACE(iid, obj, Steinberg::Vst::IHostApplication::iid, Steinberg::Vst::IHostApplication)
		*obj = NULL;
		return Steinberg::kNoInterface;
	}

	Steinberg::uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	Steinberg::uint32 PLUGIN_API release() override
	{
		Steinberg::uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

	Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
	{
		wcsncpy_s((wchar_t*)name, 128, L"Equalizer APO", _TRUNCATE);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override
	{
		return VST3HostObjects::createInstance(cid, iid, obj);
	}

private:
	volatile LONG refCount = 1;
};

// Detach the host context from the factory before either side goes away, so
// the module never holds a dangling host pointer.
void clearFactoryHostContext(Steinberg::IPluginFactory* factory, Steinberg::IPtr<Steinberg::FUnknown>& context) noexcept
{
	if (factory != nullptr && context != nullptr)
	{
		Steinberg::TUID factory3Iid;
		Steinberg::IPluginFactory3::iid.toTUID(factory3Iid);
		Steinberg::IPluginFactory3* rawFactory3 = nullptr;
		if (factory->queryInterface(factory3Iid, (void**)&rawFactory3) == Steinberg::kResultOk && rawFactory3 != nullptr)
		{
			auto factory3 = Steinberg::IPtr<Steinberg::IPluginFactory3>::adopt(rawFactory3);
			factory3->setHostContext(nullptr);
		}
	}
	context.reset();
}
}

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
			wstring installPath = WindowsRegistry::readValue(APP_REGPATH, L"InstallPath");
			defaultPluginPath = installPath + L"\\VSTPlugins";
		}
		catch (const RegistryError& e)
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
	return factory.get();
}

const Steinberg::PClassInfo& VSTPluginLibrary::getVST3ClassInfo() const
{
	return vst3ClassInfo;
}

const std::string& VSTPluginLibrary::getVST3SubCategories() const
{
	return vst3SubCategories;
}

bool VSTPluginLibrary::loadFunctions()
{
	// Detect the format from the loaded module, not from its file extension.
	// A raw VST3 module may be named .dll, and a file named .vst3 is not proof
	// that it implements the VST3 factory ABI. Prefer VST3 when a module happens
	// to expose both entry points.
	GetPluginFactory = (getPluginFactoryFunc)GetProcAddress(module.get(), "GetPluginFactory");
	VSTPluginMain = (vstPluginMain)GetProcAddress(module.get(), "VSTPluginMain");
	if (GetPluginFactory != NULL)
	{
		vst3 = true;
		// InitDll/ExitDll are optional in the module ABI; only
		// GetPluginFactory is mandatory.
		InitDll = (vst3ModuleEntryFunc)GetProcAddress(module.get(), "InitDll");
		ExitDll = (vst3ModuleEntryFunc)GetProcAddress(module.get(), "ExitDll");
		VSTPluginMain = nullptr;
		return true;
	}

	vst3 = false;
	InitDll = nullptr;
	ExitDll = nullptr;
	return VSTPluginMain != NULL;
}

int VSTPluginLibrary::customInitialize()
{
	if (!vst3)
		return 0;

	// Standard Windows VST3 module lifecycle: InitDll before the factory,
	// ExitDll after the last release. A module that exports InitDll may
	// legitimately refuse to load. On every failure path below the caller
	// (AbstractLibrary::initialize) runs customUninitialize(), which unwinds
	// whatever this method managed to set up.
	if (InitDll != nullptr)
	{
		if (!InitDll())
			return LOADING_FAILED;
		vst3ModuleInitialized = true;
	}

	factory.reset(GetPluginFactory());
	if (!factory)
		return FUNCTIONS_MISSING;

	Steinberg::TUID factory3Iid;
	Steinberg::IPluginFactory3::iid.toTUID(factory3Iid);
	Steinberg::IPluginFactory3* rawFactory3 = nullptr;
	if (factory->queryInterface(factory3Iid, (void**)&rawFactory3) == Steinberg::kResultOk && rawFactory3 != nullptr)
	{
		auto factory3 = Steinberg::IPtr<Steinberg::IPluginFactory3>::adopt(rawFactory3);
		vst3FactoryHostContext = Steinberg::IPtr<Steinberg::FUnknown>::adopt(
			static_cast<Steinberg::Vst::IHostApplication*>(new VST3FactoryHostContext()));
		if (factory3->setHostContext(vst3FactoryHostContext.get()) != Steinberg::kResultOk)
			return LOADING_FAILED;
	}

	for (Steinberg::int32 i = 0; i < factory->countClasses(); i++)
	{
		Steinberg::PClassInfo info;
		if (factory->getClassInfo(i, &info) == Steinberg::kResultOk
			&& strcmp(info.category, kVstAudioEffectClass) == 0)
		{
			vst3ClassInfo = info;
			vst3SubCategories.clear();
			Steinberg::TUID factory2Iid;
			Steinberg::IPluginFactory2::iid.toTUID(factory2Iid);
			Steinberg::IPluginFactory2* rawFactory2 = nullptr;
			if (factory->queryInterface(factory2Iid, (void**)&rawFactory2) == Steinberg::kResultOk && rawFactory2 != nullptr)
			{
				auto factory2 = Steinberg::IPtr<Steinberg::IPluginFactory2>::adopt(rawFactory2);
				Steinberg::PClassInfo2 info2;
				if (factory2->getClassInfo2(i, &info2) == Steinberg::kResultOk)
					vst3SubCategories = info2.subCategories;
			}
			return 0;
		}
	}

	return FUNCTIONS_MISSING;
}

void VSTPluginLibrary::customUninitialize() noexcept
{
	releasePluginFactory();
}

void VSTPluginLibrary::releasePluginFactory() noexcept
{
	// Reverse of customInitialize: detach the host context while the factory
	// is alive, drop the factory, then let the module tear itself down.
	clearFactoryHostContext(factory.get(), vst3FactoryHostContext);
	factory.reset();
	if (vst3ModuleInitialized && ExitDll != nullptr)
		ExitDll();
	vst3ModuleInitialized = false;
	GetPluginFactory = nullptr;
	VSTPluginMain = nullptr;
}

VSTPluginLibrary::~VSTPluginLibrary()
{
	// AbstractLibrary's base destructor unloads the module after this derived
	// destructor returns. Release module-owned VST3 objects while their code is
	// still resident.
	releasePluginFactory();
}

VSTPluginLibrary::VSTPluginLibrary(const wstring& libPath)
	: libPath(libPath), loadPath(libPath)
{
	wchar_t extension[_MAX_EXT];
	_wsplitpath_s(libPath.c_str(), NULL, 0, NULL, 0, NULL, 0, extension, _MAX_EXT);
	vst3PathHint = _wcsicmp(extension, L".vst3") == 0;
	vst3 = vst3PathHint;
	if (vst3PathHint)
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

	// Audit #250 F031: the path comes from a user-written config line, and
	// a >= MAX_PATH value used to trip wcscpy_s's invalid-parameter handler
	// and terminate the process. Build the paths dynamically instead - a
	// too-long path then simply fails to resolve.
	std::wstring platformBase = libPath;
	while (!platformBase.empty()
		&& (platformBase.back() == L'\\' || platformBase.back() == L'/'))
	{
		platformBase.pop_back();
	}
	platformBase += L"\\Contents\\";
	platformBase += platformDir;

	WIN32_FIND_DATAW findData;
	winutil::UniqueFindHandle find(
		FindFirstFileW((platformBase + L"\\*.vst3").c_str(), &findData));
	if (!find)
		return libPath;

	return platformBase + L"\\" + findData.cFileName;
}
