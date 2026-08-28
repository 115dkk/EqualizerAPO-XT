/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The FUnknown boilerplate every host-side VST3 object used to repeat
	(audit #275 C4/TD-25): the interlocked refcount with delete-on-zero and
	the linear iid match over the object's interface list. Deriving from
	VST3RefCounted<I1, I2, ...> replaces the hand-written copies; a query
	for FUnknown maps to the first interface, exactly as each copy did.

	Spelled without the SDK's QUERY_INTERFACE macro: cppcheck cannot expand
	SDK macros in a standalone header and fails the gate on them.

	Objects deriving from this are heap-allocated and die through release()
	(the callers hand them straight to IPtr::adopt), which is why release()
	may delete and why the destructor is virtual and protected.
*/

#pragma once

#include <tuple>

#include "pluginterfaces/base/funknown.h"

template <typename... Interfaces>
class VST3RefCounted : public Interfaces...
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
	{
		if (obj == NULL)
			return Steinberg::kInvalidArgument;
		*obj = NULL;
		if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid))
			*obj = static_cast<FirstInterface*>(this);
		else
			(matchInterface<Interfaces>(iid, obj) || ...);
		if (*obj == NULL)
			return Steinberg::kNoInterface;
		addRef();
		return Steinberg::kResultOk;
	}

	Steinberg::uint32 PLUGIN_API addRef() override { return InterlockedIncrement(&refCount); }
	Steinberg::uint32 PLUGIN_API release() override
	{
		Steinberg::uint32 result = InterlockedDecrement(&refCount);
		if (result == 0)
			delete this;
		return result;
	}

protected:
	virtual ~VST3RefCounted() = default;

private:
	using FirstInterface = std::tuple_element_t<0, std::tuple<Interfaces...>>;

	template <typename Interface>
	bool matchInterface(const Steinberg::TUID iid, void** obj)
	{
		if (Steinberg::FUnknownPrivate::iidEqual(iid, Interface::iid))
			*obj = static_cast<Interface*>(this);
		return *obj != NULL;
	}

	volatile LONG refCount = 1;
};
