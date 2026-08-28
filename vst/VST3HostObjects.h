/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    Host-side VST3 utility objects shared by the factory host context
    (VSTPluginLibrary) and the per-instance host context (VST3HostContext):
    the IMessage/IAttributeList pair a plug-in asks its host to create via
    IHostApplication::createInstance. Split plug-ins route their
    component<->controller IConnectionPoint traffic through these objects,
    so a host without them silently breaks that channel (parameter sync,
    program lists). Ported from the ripDZL fork's VST3 compatibility work
    (github.com/ripDZL/EqualizerAPO-XT/pull/1), restated in this codebase's
    host-object idiom.
*/

#pragma once

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "VST3RefCounted.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/futils.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstmessage.h"

namespace VST3HostObjects
{
// The maps are mutex-guarded because IConnectionPoint messages may be
// produced and consumed on different threads (a plug-in's private worker
// posting to the controller on the GUI thread).
class AttributeList : public VST3RefCounted<Steinberg::Vst::IAttributeList>
{
public:
	Steinberg::tresult PLUGIN_API setInt(AttrID id, Steinberg::int64 value) override
	{
		if (id == NULL)
			return Steinberg::kInvalidArgument;
		std::lock_guard<std::mutex> lock(dataMutex);
		intValues[id] = value;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API getInt(AttrID id, Steinberg::int64& value) override
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		auto it = id != NULL ? intValues.find(id) : intValues.end();
		if (it == intValues.end())
			return Steinberg::kResultFalse;
		value = it->second;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API setFloat(AttrID id, double value) override
	{
		if (id == NULL)
			return Steinberg::kInvalidArgument;
		std::lock_guard<std::mutex> lock(dataMutex);
		floatValues[id] = value;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API getFloat(AttrID id, double& value) override
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		auto it = id != NULL ? floatValues.find(id) : floatValues.end();
		if (it == floatValues.end())
			return Steinberg::kResultFalse;
		value = it->second;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API setString(AttrID id, const Steinberg::Vst::TChar* value) override
	{
		if (id == NULL || value == NULL)
			return Steinberg::kInvalidArgument;
		std::lock_guard<std::mutex> lock(dataMutex);
		std::vector<Steinberg::Vst::TChar>& stored = stringValues[id];
		stored.assign(value, value + stringLength(value) + 1);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API getString(AttrID id, Steinberg::Vst::TChar* value, Steinberg::uint32 sizeInBytes) override
	{
		if (value == NULL || sizeInBytes < sizeof(Steinberg::Vst::TChar))
			return Steinberg::kInvalidArgument;
		std::lock_guard<std::mutex> lock(dataMutex);
		auto it = id != NULL ? stringValues.find(id) : stringValues.end();
		if (it == stringValues.end() || sizeInBytes < it->second.size() * sizeof(Steinberg::Vst::TChar))
			return Steinberg::kResultFalse;
		memcpy(value, it->second.data(), it->second.size() * sizeof(Steinberg::Vst::TChar));
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API setBinary(AttrID id, const void* value, Steinberg::uint32 sizeInBytes) override
	{
		if (id == NULL || (value == NULL && sizeInBytes != 0))
			return Steinberg::kInvalidArgument;
		std::lock_guard<std::mutex> lock(dataMutex);
		const char* bytes = static_cast<const char*>(value);
		binaryValues[id] = sizeInBytes == 0 ? std::vector<char>() : std::vector<char>(bytes, bytes + sizeInBytes);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API getBinary(AttrID id, const void*& value, Steinberg::uint32& sizeInBytes) override
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		auto it = id != NULL ? binaryValues.find(id) : binaryValues.end();
		if (it == binaryValues.end())
			return Steinberg::kResultFalse;
		value = it->second.empty() ? NULL : it->second.data();
		sizeInBytes = (Steinberg::uint32)it->second.size();
		return Steinberg::kResultOk;
	}

private:
	static size_t stringLength(const Steinberg::Vst::TChar* value)
	{
		size_t length = 0;
		while (value[length] != 0)
			length++;
		return length;
	}

	std::mutex dataMutex;
	std::unordered_map<std::string, Steinberg::int64> intValues;
	std::unordered_map<std::string, double> floatValues;
	std::unordered_map<std::string, std::vector<Steinberg::Vst::TChar>> stringValues;
	std::unordered_map<std::string, std::vector<char>> binaryValues;
};

class Message : public VST3RefCounted<Steinberg::Vst::IMessage>
{
public:
	Message() : attributes(new AttributeList()) {}

	Steinberg::FIDString PLUGIN_API getMessageID() override { return messageId.empty() ? NULL : messageId.c_str(); }
	void PLUGIN_API setMessageID(Steinberg::FIDString id) override { messageId = id != NULL ? id : ""; }
	Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override { return attributes; }

private:
	~Message() override { attributes->release(); }

	std::string messageId;
	AttributeList* attributes;
};

// IHostApplication::createInstance contract: the host manufactures the two
// object kinds plug-ins are allowed to request. Everything else stays
// kNoInterface, matching Steinberg's reference host.
inline Steinberg::tresult createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj)
{
	if (obj == NULL)
		return Steinberg::kInvalidArgument;
	*obj = NULL;
	if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IMessage::iid)
		&& (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMessage::iid)
		|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)))
	{
		*obj = static_cast<Steinberg::Vst::IMessage*>(new Message());
		return Steinberg::kResultOk;
	}
	if (Steinberg::FUnknownPrivate::iidEqual(cid, Steinberg::Vst::IAttributeList::iid)
		&& (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IAttributeList::iid)
		|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)))
	{
		*obj = static_cast<Steinberg::Vst::IAttributeList*>(new AttributeList());
		return Steinberg::kResultOk;
	}
	return Steinberg::kNoInterface;
}
}
