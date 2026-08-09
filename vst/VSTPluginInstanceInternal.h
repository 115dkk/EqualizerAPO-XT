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

// Internal header shared between the VSTPluginInstance translation units.
// It carries the definitions of the nested helper classes VST3MemoryStream and
// VST3HostContext, which are used across more than one of those translation
// units (VST3MemoryStream by VSTPluginInstance.VST3.cpp and
// VSTPluginInstance.State.cpp; VST3HostContext by VSTPluginInstance.VST3.cpp and
// VSTPluginInstance.Editor.cpp). Defining a nested class once here keeps the
// One Definition Rule satisfied without giving file-static helpers cross-TU
// linkage. This header is NOT part of the public API; it is only included by the
// VSTPluginInstance*.cpp files, each of which includes "stdafx.h" first.
//
// The using-directives below mirror the ones at file scope in every consuming
// translation unit, so the class bodies stay token-for-token identical to their
// previous location in VSTPluginInstance.cpp. No new namespace pollution is
// introduced because each consumer already pulls in the same namespaces.

#include <vector>
#include "VST3HostObjects.h"
#include "VSTPluginInstance.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

class VSTPluginInstance::VST3MemoryStream : public IBStream
{
public:
	VST3MemoryStream() {}
	VST3MemoryStream(const vector<char>& data) : data(data) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IBStream)
		QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	uint32 PLUGIN_API release() override
	{
		uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

	tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead = nullptr) override
	{
		int32 available = (int32)min<size_t>(numBytes, data.size() - min<size_t>(position, data.size()));
		if (available > 0)
			memcpy(buffer, data.data() + position, available);
		position += available;
		if (numBytesRead != NULL)
			*numBytesRead = available;
		return kResultOk;
	}

	tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten = nullptr) override
	{
		if (numBytes <= 0)
		{
			if (numBytesWritten != NULL)
				*numBytesWritten = 0;
			return kResultOk;
		}
		if (position + numBytes > data.size())
			data.resize(position + numBytes);
		memcpy(data.data() + position, buffer, numBytes);
		position += numBytes;
		if (numBytesWritten != NULL)
			*numBytesWritten = numBytes;
		return kResultOk;
	}

	tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result = nullptr) override
	{
		int64 newPosition = 0;
		if (mode == kIBSeekSet)
			newPosition = pos;
		else if (mode == kIBSeekCur)
			newPosition = (int64)position + pos;
		else if (mode == kIBSeekEnd)
			newPosition = (int64)data.size() + pos;
		if (newPosition < 0)
			newPosition = 0;
		position = (size_t)newPosition;
		if (position > data.size())
			data.resize(position);
		if (result != NULL)
			*result = (int64)position;
		return kResultOk;
	}

	tresult PLUGIN_API tell(int64* pos) override
	{
		if (pos == NULL)
			return kInvalidArgument;
		*pos = (int64)position;
		return kResultOk;
	}

	const vector<char>& getData() const { return data; }

private:
	volatile LONG refCount = 1;
	vector<char> data;
	size_t position = 0;
};

class VSTPluginInstance::VST3HostContext : public IHostApplication, public IComponentHandler,
	public IComponentHandler2, public IPlugFrame, public IPlugInterfaceSupport
{
public:
	VST3HostContext(VSTPluginInstance* instance) : instance(instance) {}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		QUERY_INTERFACE(iid, obj, FUnknown::iid, IHostApplication)
		QUERY_INTERFACE(iid, obj, IHostApplication::iid, IHostApplication)
		QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
		QUERY_INTERFACE(iid, obj, IComponentHandler2::iid, IComponentHandler2)
		QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
		QUERY_INTERFACE(iid, obj, IPlugInterfaceSupport::iid, IPlugInterfaceSupport)
		*obj = NULL;
		return kNoInterface;
	}

	uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	uint32 PLUGIN_API release() override
	{
		uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

	tresult PLUGIN_API getName(String128 name) override
	{
		wcsncpy_s((wchar_t*)name, 128, L"Equalizer APO", _TRUNCATE);
		return kResultOk;
	}

	tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override
	{
		// IMessage/IAttributeList for the component<->controller connection;
		// everything else stays unavailable (VST3HostObjects).
		return VST3HostObjects::createInstance(cid, iid, obj);
	}

	tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
	tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
	{
		// A GUI edit only lives in the controller until the host feeds it to
		// the processor through IParameterChanges; the instance queues it for
		// the next process block (or flushes immediately while idle).
		if (instance != NULL)
			instance->onVST3ParameterEdit(id, value);
		return kResultOk;
	}
	tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
	// Honest answer: this host does not re-read parameter layout / latency on
	// request. Claiming kResultOk here made plug-ins assume a refresh
	// happened.
	tresult PLUGIN_API restartComponent(int32) override { return kNotImplemented; }

	tresult PLUGIN_API setDirty(TBool state) override
	{
		if (state && instance != NULL)
			instance->onAutomate();
		return kResultOk;
	}
	tresult PLUGIN_API requestOpenEditor(FIDString name) override
	{
		// The host opens editors on user action only; unknown view types are
		// refused outright.
		return name == nullptr || strcmp(name, ViewType::kEditor) == 0 ? kNotImplemented : kResultFalse;
	}
	tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
	tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }

	tresult PLUGIN_API isPlugInterfaceSupported(const TUID iid) override
	{
		return FUnknownPrivate::iidEqual(iid, IComponent::iid)
			|| FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid)
			|| FUnknownPrivate::iidEqual(iid, IEditController::iid)
			|| FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IConnectionPoint::iid)
			|| FUnknownPrivate::iidEqual(iid, IPlugView::iid)
			|| FUnknownPrivate::iidEqual(iid, IPlugViewContentScaleSupport::iid)
			? kResultTrue : kResultFalse;
	}

	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override
	{
		if (view != NULL && newSize != NULL)
		{
			// Resize the host windows first so the view lays out against its
			// final geometry, and forward the view's own verdict.
			if (instance != NULL)
				instance->onSizeWindow(newSize->getWidth(), newSize->getHeight());
			return view->onSize(newSize);
		}
		return kInvalidArgument;
	}

private:
	volatile LONG refCount = 1;
	VSTPluginInstance* instance;
};
