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
#include <inttypes.h>
#include "../Version.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"

using namespace std;

#define equalizerApoVSTID VST_FOURCC('E', 'A', 'P', 'O');

vst_time_info* VSTPluginInstance::hostTimeInfo()
{
	vstTime.sampleRate = getSampleRate();
	return &vstTime;
}

static intptr_t callback(struct vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value, const char* ptr, float opt)
{
	VSTPluginInstance* instance = effect != NULL ? (VSTPluginInstance*)effect->host_internal : NULL;
#ifdef _DEBUG
	printf("vst: %p opcode: %d index: %d value: %" PRIdPTR " ptr: %p opt: %f host_internal: %p\n",
		effect, opcode, index, value, ptr, opt, effect != NULL ? effect->host_internal : NULL);
	fflush(stdout);
#endif

	switch (opcode)
	{
	case VST_HOST_OPCODE_VST_VERSION:
		return VST_VERSION_2_4_0_0;

	case VST_HOST_OPCODE_CURRENT_EFFECT_ID:
		return equalizerApoVSTID;

	// Audit #250 F027: these two cases used to be spelled with the effect
	// constants (0x30/0x31). In the host callback 0x31 is
	// VST_HOST_OPCODE_GET_INPUT_SPEAKER_ARRANGEMENT, whose contract returns
	// a pointer - answering it with a version integer invited plugins to
	// dereference it inside audiodg. The host product/vendor queries are
	// 0x21/0x22.
	case VST_HOST_OPCODE_PRODUCT_NAME:
		strcpy_s((char*) ptr, 64, "Equalizer APO");
		return 1;

	case VST_HOST_OPCODE_VENDOR_VERSION:
		// The low byte (a fourth version component) is always zero.
		return (intptr_t) (MAJOR << 24 | MINOR << 16 | REVISION << 8);

	case VST_HOST_OPCODE_GET_INPUT_SPEAKER_ARRANGEMENT:
		// Pointer contract; we do not provide an arrangement. Returning 0
		// (null) is the documented "not supported" answer.
		return 0;

	case VST_HOST_OPCODE_PIN_CONNECTED:
		if (instance != NULL)
			return index < instance->getUsedChannelCount() ? 0 : 1;
		else
			return 0;

	case VST_HOST_OPCODE_IO_NEED_IDLE:
		return effect != NULL ? effect->control(effect, VST_HOST_OPCODE_KEEPALIVE_OR_IDLE, 0, 0, NULL, 0.0f) : 0;

	case VST_HOST_OPCODE_EDITOR_UPDATE:
		return effect != NULL ? effect->control(effect, VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f) : 0;

	case VST_HOST_OPCODE_GET_TIME:
		if (instance != NULL)
			return (intptr_t)instance->hostTimeInfo();
		return 0;

	case VST_HOST_OPCODE_GET_SAMPLE_RATE:
		if (instance != NULL)
			return (intptr_t)instance->getSampleRate();
		return 0;

	case VST_HOST_OPCODE_GET_ACTIVE_THREAD:
		if (instance != NULL)
			return instance->getProcessLevel();
		return 0;

	case VST_HOST_OPCODE_LANGUAGE:
		if (instance != NULL)
			return instance->getLanguage();
		return 0;

	case VST_HOST_OPCODE_GET_REPLACE_OR_ACCUMULATE:
		return 1;

	case VST_HOST_OPCODE_EDITOR_RESIZE:
		if (instance != NULL)
		{
			instance->onSizeWindow((int)index, (int)value);
			return 0;
		}
		return 1;

	case VST_HOST_OPCODE_SUPPORTS:
		{
			const char* s = (const char*)ptr;
#ifdef _DEBUG
			printf("VST canDo: %s\n", s);
			fflush(stdout);
#endif
			if (strcmp(s, "startStopProcess") == 0 ||
				strcmp(s, "sizeWindow") == 0)
				return 1;
		}
		return 0;

	case VST_HOST_OPCODE_AUTOMATE:
		if (instance != NULL)
			instance->onAutomate();
		return 0;

	case VST_HOST_OPCODE_PARAM_START_EDIT:
	case VST_HOST_OPCODE_PARAM_STOP_EDIT:
	case VST_HOST_OPCODE_KEEPALIVE_OR_IDLE:
	case VST_HOST_OPCODE_WANT_MIDI:
		return 0;
	}

	return 0;
}

VSTPluginInstance::Vst2LoadResult VSTPluginInstance::initializeVST2()
{
	Vst2LoadResult result = Vst2LoadResult::Loaded;

	__try
	{
		vst_effect_t* candidate = library->VSTPluginMain(callback);
		if (candidate == NULL)
		{
			result = Vst2LoadResult::NoEntryPoint;
		}
		else if (candidate->magic_number != VST_MAGICNUMBER)
		{
			result = Vst2LoadResult::WrongMagicNumber;
		}
		else
		{
			effect.reset(candidate);
			effect->host_internal = this;
			effect->control(effect.get(), VST_EFFECT_OPCODE_INITIALIZE, 0, 0, NULL, 0.0f);

			usedChannelCount = max(numInputs(), numOutputs());
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = Vst2LoadResult::Crashed;
	}

	return result;
}
