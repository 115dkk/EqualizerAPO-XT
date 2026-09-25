/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The IBStream the host hands a VST3 plug-in for getState/setState: a
	growable byte buffer with a cursor, bounded by maximumSize. Header-only
	so the host tests can exercise it without a plug-in.
*/

#pragma once

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "VST3RefCounted.h"

class VST3MemoryStream : public VST3RefCounted<Steinberg::IBStream>
{
public:
	// The largest the stream may grow (audit #348 TD-48). Seek and write take
	// a position from the plug-in; without a bound, one wild seek made the
	// host allocate whatever it named (and a failed allocation threw across
	// the plug-in's COM boundary). A state this large would be a third of a
	// gigabyte of base64 on one config line; real plug-in states are far
	// below it.
	static constexpr size_t maximumSize = 256u * 1024u * 1024u;

	VST3MemoryStream() {}
	explicit VST3MemoryStream(const std::vector<char>& data) : data(data) {}

	Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes,
		Steinberg::int32* numBytesRead = nullptr) override
	{
		// A negative count used to convert to a huge size_t and copy the
		// whole remainder into the plug-in's buffer.
		Steinberg::int32 available = 0;
		if (numBytes > 0 && position < data.size())
			available = (Steinberg::int32)(std::min)((size_t)numBytes, data.size() - position);
		if (available > 0)
		{
			if (buffer == NULL)
				return Steinberg::kInvalidArgument;
			memcpy(buffer, data.data() + position, available);
		}
		position += available;
		if (numBytesRead != NULL)
			*numBytesRead = available;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes,
		Steinberg::int32* numBytesWritten = nullptr) override
	{
		if (numBytesWritten != NULL)
			*numBytesWritten = 0;
		if (numBytes <= 0)
			return Steinberg::kResultOk;
		if (buffer == NULL)
			return Steinberg::kInvalidArgument;
		// Checked in this order so neither side can wrap.
		if (position > maximumSize || (size_t)numBytes > maximumSize - position)
			return Steinberg::kOutOfMemory;
		if (position + numBytes > data.size() && !grow(position + numBytes))
			return Steinberg::kOutOfMemory;
		memcpy(data.data() + position, buffer, numBytes);
		position += numBytes;
		if (numBytesWritten != NULL)
			*numBytesWritten = numBytes;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode,
		Steinberg::int64* result = nullptr) override
	{
		Steinberg::int64 base = 0;
		if (mode == kIBSeekSet)
			base = 0;
		else if (mode == kIBSeekCur)
			base = (Steinberg::int64)position;
		else if (mode == kIBSeekEnd)
			base = (Steinberg::int64)data.size();
		else
			return Steinberg::kInvalidArgument;

		// base is a buffer offset, far from either int64 limit, so neither
		// comparison overflows. A position before the start clamps to it, as
		// it always has; one past the bound is refused and the cursor stays
		// where it was.
		if (pos > (Steinberg::int64)maximumSize - base)
			return Steinberg::kInvalidArgument;
		const size_t newPosition = pos < -base ? 0 : (size_t)(base + pos);
		if (newPosition > data.size() && !grow(newPosition))
			return Steinberg::kOutOfMemory;
		position = newPosition;
		if (result != NULL)
			*result = (Steinberg::int64)position;
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override
	{
		if (pos == NULL)
			return Steinberg::kInvalidArgument;
		*pos = (Steinberg::int64)position;
		return Steinberg::kResultOk;
	}

	const std::vector<char>& getData() const { return data; }

private:
	// Zero-fills up to size; false when the allocation fails.
	bool grow(size_t size) noexcept
	{
		try
		{
			data.resize(size);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
	}

	std::vector<char> data;
	size_t position = 0;
};
