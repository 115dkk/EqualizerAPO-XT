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

// The per-instance VST3 host context: the IHostApplication a component and
// controller are initialized with, the IComponentHandler the controller
// reports edits through, and the IPlugFrame an editor view resizes through.
// It relays those calls to its VST3Instance. Internal to the VST3Instance
// translation units; each includes "stdafx.h" first.

#include <atomic>

#include "VST3HostObjects.h"
#include "VST3Instance.h"
#include "VST3RefCounted.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"

class VST3HostContext : public VST3RefCounted<Steinberg::Vst::IHostApplication,
	Steinberg::Vst::IComponentHandler, Steinberg::Vst::IComponentHandler2,
	Steinberg::IPlugFrame, Steinberg::Vst::IPlugInterfaceSupport>
{
public:
	explicit VST3HostContext(VST3Instance* instance) : instance(instance) {}

	// Called by the instance before it releases the plug-in (audit #348
	// TD-48). A plug-in may keep this object - it is refcounted, and the
	// component, controller and view all hold it - and call it after the
	// instance is gone; from here on the calls that would reach the
	// instance are refused instead of touching freed memory. Host-level
	// services (getName, createInstance, isPlugInterfaceSupported) keep
	// answering: they need no instance.
	void detach() noexcept
	{
		instance.store(nullptr, std::memory_order_release);
	}

	Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
	{
		wcsncpy_s((wchar_t*)name, 128, L"Equalizer APO", _TRUNCATE);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override
	{
		// IMessage/IAttributeList for the component<->controller connection;
		// everything else stays unavailable (VST3HostObjects).
		return VST3HostObjects::createInstance(cid, iid, obj);
	}

	Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return attachedResult(); }
	Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override
	{
		// A GUI edit only lives in the controller until the host feeds it to
		// the processor through IParameterChanges; the instance queues it for
		// the next process block (or flushes immediately while idle).
		VST3Instance* target = attachedInstance();
		if (target == NULL)
			return Steinberg::kResultFalse;
		target->onVST3ParameterEdit(id, value);
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return attachedResult(); }
	// Honest answer: this host does not re-read parameter layout / latency on
	// request. Claiming Steinberg::kResultOk here made plug-ins assume a refresh
	// happened.
	Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override { return Steinberg::kNotImplemented; }

	Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override
	{
		VST3Instance* target = attachedInstance();
		if (target == NULL)
			return Steinberg::kResultFalse;
		if (state)
			target->onAutomate();
		return Steinberg::kResultOk;
	}
	Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString name) override
	{
		// The host opens editors on user action only; unknown view types are
		// refused outright.
		return name == nullptr || strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0 ? Steinberg::kNotImplemented : Steinberg::kResultFalse;
	}
	Steinberg::tresult PLUGIN_API startGroupEdit() override { return attachedResult(); }
	Steinberg::tresult PLUGIN_API finishGroupEdit() override { return attachedResult(); }

	Steinberg::tresult PLUGIN_API isPlugInterfaceSupported(const Steinberg::TUID iid) override
	{
		return Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponent::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAudioProcessor::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IEditController::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IConnectionPoint::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugView::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugViewContentScaleSupport::iid)
			? Steinberg::kResultTrue : Steinberg::kResultFalse;
	}

	Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override
	{
		if (view != NULL && newSize != NULL)
		{
			VST3Instance* target = attachedInstance();
			if (target == NULL)
				return Steinberg::kResultFalse;
			// Resize the host windows first so the view lays out against its
			// final geometry, and forward the view's own verdict.
			target->onSizeWindow(newSize->getWidth(), newSize->getHeight());
			return view->onSize(newSize);
		}
		return Steinberg::kInvalidArgument;
	}

private:
	VST3Instance* attachedInstance() const noexcept
	{
		return instance.load(std::memory_order_acquire);
	}

	Steinberg::tresult attachedResult() const noexcept
	{
		return attachedInstance() != NULL ? Steinberg::kResultOk : Steinberg::kResultFalse;
	}

	std::atomic<VST3Instance*> instance;
};
