// SPDX-License-Identifier: MIT

#define INIT_CLASS_IID

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <atomic>
#include <cstring>

#include "controller.h"
#include "plugin_ids.h"
#include "processor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace eapoxt::subwooferrouting::vst3
{
namespace
{

bool iidIs(const TUID iid, const FUID& expected)
{
	return FUnknownPrivate::iidEqual(iid, expected);
}

void copyTuid(TUID destination, const TUID& source)
{
	std::memcpy(destination, source, sizeof(TUID));
}

class SubwooferRoutingFactory final : public IPluginFactory3
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
	{
		if (object == nullptr)
			return kInvalidArgument;

		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginFactory::iid))
			*object = static_cast<IPluginFactory*>(this);
		else if (iidIs(iid, IPluginFactory2::iid))
			*object = static_cast<IPluginFactory2*>(this);
		else if (iidIs(iid, IPluginFactory3::iid))
			*object = static_cast<IPluginFactory3*>(this);
		else
		{
			*object = nullptr;
			return kNoInterface;
		}

		addRef();
		return kResultOk;
	}

	uint32 PLUGIN_API addRef() override
	{
		return ++refCount_;
	}

	uint32 PLUGIN_API release() override
	{
		return --refCount_;
	}

	tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
	{
		if (info == nullptr)
			return kInvalidArgument;
		*info = PFactoryInfo(kVendor, kUrl, kEmail, PFactoryInfo::kNoFlags);
		return kResultOk;
	}

	int32 PLUGIN_API countClasses() override
	{
		return 2;
	}

	tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
	{
		if (info == nullptr || index < 0 || index >= countClasses())
			return kInvalidArgument;

		std::memset(info, 0, sizeof(*info));
		if (index == 0)
		{
			copyTuid(info->cid, kComponentCid);
			info->cardinality = PClassInfo::kManyInstances;
			strcpy_s(info->category, kVstAudioEffectClass);
			strcpy_s(info->name, kPluginName);
		}
		else
		{
			copyTuid(info->cid, kControllerCid);
			info->cardinality = PClassInfo::kManyInstances;
			strcpy_s(info->category, kVstComponentControllerClass);
			strcpy_s(info->name, kControllerName);
		}
		return kResultOk;
	}

	tresult PLUGIN_API createInstance(
		FIDString cid,
		FIDString iid,
		void** object) override
	{
		if (cid == nullptr || iid == nullptr || object == nullptr)
			return kInvalidArgument;

		*object = nullptr;
		FUnknown* instance = nullptr;
		if (FUnknownPrivate::iidEqual(cid, kComponentCid))
			instance = createSubwooferRoutingProcessor();
		else if (FUnknownPrivate::iidEqual(cid, kControllerCid))
			instance = createSubwooferRoutingController();
		else
			return kNoInterface;

		const tresult result = instance->queryInterface(iid, object);
		instance->release();
		return result;
	}

	tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
	{
		if (info == nullptr || index < 0 || index >= countClasses())
			return kInvalidArgument;

		std::memset(info, 0, sizeof(*info));
		if (index == 0)
		{
			copyTuid(info->cid, kComponentCid);
			info->cardinality = PClassInfo::kManyInstances;
			strcpy_s(info->category, kVstAudioEffectClass);
			strcpy_s(info->name, kPluginName);
			strcpy_s(info->subCategories, kSubCategories);
		}
		else
		{
			copyTuid(info->cid, kControllerCid);
			info->cardinality = PClassInfo::kManyInstances;
			strcpy_s(info->category, kVstComponentControllerClass);
			strcpy_s(info->name, kControllerName);
		}

		strcpy_s(info->vendor, kVendor);
		strcpy_s(info->version, kVersion);
		strcpy_s(info->sdkVersion, kSdkVersion);
		return kResultOk;
	}

	tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
	{
		if (info == nullptr)
			return kInvalidArgument;

		PClassInfo2 ascii;
		if (getClassInfo2(index, &ascii) != kResultOk)
			return kInvalidArgument;
		info->fromAscii(ascii);
		return kResultOk;
	}

	tresult PLUGIN_API setHostContext(FUnknown*) override
	{
		return kResultOk;
	}

private:
	std::atomic<uint32> refCount_{1};
};

SubwooferRoutingFactory factory;
std::atomic<bool> moduleInitialized{false};

}
}

extern "C" __declspec(dllexport) bool InitDll()
{
	eapoxt::subwooferrouting::vst3::moduleInitialized.store(
		true,
		std::memory_order_release);
	return true;
}

extern "C" __declspec(dllexport) bool ExitDll()
{
	eapoxt::subwooferrouting::vst3::moduleInitialized.store(
		false,
		std::memory_order_release);
	return true;
}

extern "C" __declspec(dllexport) Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
	using namespace eapoxt::subwooferrouting::vst3;
	if (!moduleInitialized.load(std::memory_order_acquire))
		return nullptr;
	factory.addRef();
	return &factory;
}
