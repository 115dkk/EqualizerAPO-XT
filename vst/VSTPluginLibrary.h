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

#pragma once

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include "AbstractLibrary.h"
// aeffectx.h for the VST2 entry-point signature and the two base VST3 headers for
// the factory this class owns. It deliberately does not include
// VSTPluginInstance.h: nothing here refers to an instance, and that header pulls
// in the whole VST3 audio-processor and edit-controller set - which every
// translation unit that only wanted getInstance()/getLibPath() was paying for.
// The six Editor units that include this one use four members between them.
#include "aeffectx.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"

class VSTPluginLibrary : public AbstractLibrary
{
public:
	~VSTPluginLibrary() override;

	static std::shared_ptr<VSTPluginLibrary> getInstance(const std::wstring& libPath);
	static std::wstring getDefaultPluginPath();

	std::wstring getLibPath() override;
	std::wstring getLoadPath() override;
	bool isVST3() const;

	typedef vst_effect_t* (* vstPluginMain)(vst_host_callback_t audioMaster);
	vstPluginMain VSTPluginMain = nullptr;
	typedef Steinberg::IPluginFactory* (PLUGIN_API* getPluginFactoryFunc)();
	getPluginFactoryFunc GetPluginFactory;
	Steinberg::IPluginFactory* getFactory() const;
	const Steinberg::PClassInfo& getVST3ClassInfo() const;
	// The audio class's PClassInfo2 subCategories string ("Fx|Up-Downmix"
	// etc.), empty when the factory only provides the basic class info. The
	// host uses it to recognize upmixer-type plugins whose input bus must
	// stay stereo while the output bus spans the device.
	const std::string& getVST3SubCategories() const;

protected:
	bool loadFunctions() override;
	int customInitialize() override;
	void customUninitialize() noexcept override;

private:
	struct FactoryDeleter
	{
		void operator()(Steinberg::IPluginFactory* value) const noexcept
		{
			if (value != nullptr)
				value->release();
		}
	};

	VSTPluginLibrary(const std::wstring& libPath);
	void releasePluginFactory() noexcept;
	static std::wstring resolveVST3ModulePath(const std::wstring& libPath);
	static std::unordered_map<std::wstring, std::weak_ptr<VSTPluginLibrary>> instanceMap;
	static std::wstring defaultPluginPath;
	std::wstring libPath;
	std::wstring loadPath;
	// The extension is used only to resolve a standard .vst3 bundle. Once the
	// module is loaded, `vst3` is set from its exported ABI entry points.
	bool vst3PathHint = false;
	bool vst3 = false;
	std::unique_ptr<Steinberg::IPluginFactory, FactoryDeleter> factory;
	// The factory-level host context handed to IPluginFactory3::setHostContext
	// before any class is created; some plug-ins refuse to instantiate without
	// one. Owned here (IPtr) and detached from the factory before the factory
	// itself is released.
	Steinberg::IPtr<Steinberg::FUnknown> vst3FactoryHostContext;
	// InitDll/ExitDll are plain C (cdecl) exports in Steinberg's Windows module
	// ABI. PLUGIN_API is __stdcall on Win32, where the mismatch would corrupt
	// the stack (x64/ARM64 mask it); keep the raw calling convention.
	typedef bool (* vst3ModuleEntryFunc)();
	vst3ModuleEntryFunc InitDll = nullptr;
	vst3ModuleEntryFunc ExitDll = nullptr;
	bool vst3ModuleInitialized = false;
	Steinberg::PClassInfo vst3ClassInfo;
	std::string vst3SubCategories;
};
