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

// VST2 hosting behind VSTPluginInstance (audit #348 C4/TD-40): the AEffect,
// its host callback (the audioMaster), and every VST2 operation of an
// instance - loading with its __try/__except guard, processing, the editor
// and chunk/parameter state.

#include "stdafx.h"
#include <inttypes.h>
#include "../Version.h"
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include "services/logging/Logging.h"
#include "aeffectx.h"
#include "VSTPluginLibrary.h"
#include "VSTFormatInstance.h"
#include "VSTChunkBase64.h"

using namespace std;
using vstchunk::decodeBase64;
using vstchunk::encodeBase64;

#define equalizerApoVSTID VST_FOURCC('E', 'A', 'P', 'O');

namespace
{
struct VST2EffectDeleter
{
	void operator()(vst_effect_t* effect) const noexcept
	{
		if (effect != NULL)
		{
			__try
			{
				effect->control(effect, VST_EFFECT_OPCODE_DESTROY, 0, 0, NULL, 0.0f);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				// A broken plugin must not take down the host while its RAII owner
				// unwinds. The plugin's memory is no longer safely reclaimable here.
			}
		}
	}
};

using VST2EffectPtr = std::unique_ptr<vst_effect_t, VST2EffectDeleter>;

class VST2Instance final : public VSTFormatInstance
{
public:
	VST2Instance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
		: VSTFormatInstance(library, processLevel)
	{
	}

	~VST2Instance() override
	{
		// The effect may still call back while it is destroyed; nothing it
		// reaches through the callback may run a caller's closure any more.
		automateFunc = nullptr;
		sizeWindowFunc = nullptr;
		effect.reset();
	}

	bool initialize() override;

	int numInputs() const override
	{
		if (effect == NULL)
			return 0;

		return effect->num_inputs;
	}

	int numOutputs() const override
	{
		if (effect == NULL)
			return 0;

		return effect->num_outputs;
	}

	// VST2 channel counts are fixed by the effect; negotiation only checks
	// whether they are wide enough.
	bool negotiateChannelCount(int channelCount, const vector<wstring>&) override
	{
		return max(numInputs(), numOutputs()) >= channelCount;
	}

	bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
		const vector<wstring>&, const vector<wstring>&) override
	{
		return numInputs() >= inputChannelCount && numOutputs() >= outputChannelCount;
	}

	// Bus layouts are a VST3 contract.
	bool negotiateBusLayouts(VST3BusLayout, VST3BusLayout, int,
		const vector<wstring>&, const vector<wstring>&) override
	{
		return false;
	}

	optional<VST3BusLayout> negotiatedInputLayout() const override { return nullopt; }
	optional<VST3BusLayout> negotiatedOutputLayout() const override { return nullopt; }

	bool canProcessNow() const override { return vst2Processing; }

	int uniqueID() const override
	{
		if (effect == NULL)
			return 0;

		return effect->unique_id;
	}

	wstring getName() const override
	{
		if (effect == NULL)
			return L"";

		char buf[256];
		memset(buf, 0, sizeof(buf));
		effect->control(effect.get(), VST_EFFECT_OPCODE_EFFECT_NAME, 0, 0, buf, 0.0f);
		buf[255] = '\0'; // just to be sure

		return wintext::toWideString(buf, CP_UTF8);
	}

	int getInitialDelay() const override
	{
		if (effect == NULL)
			return 0;

		return effect->delay;
	}

	void prepareForProcessing(float sampleRate, int blockSize) override
	{
		if (effect == NULL)
			return;

		this->sampleRate = sampleRate;
		effect->control(effect.get(), VST_EFFECT_OPCODE_SET_SAMPLE_RATE, 0, 0, NULL, sampleRate);
		effect->control(effect.get(), VST_EFFECT_OPCODE_SET_BLOCK_SIZE, 0, blockSize, NULL, 0.0f);
		refreshSampleWidths();
	}

	void writeToEffect(const wstring& chunkData, const unordered_map<wstring, float>& paramMap) override;
	void readFromEffect(wstring& chunkData, unordered_map<wstring, float>& paramMap) const override;

	void startProcessing() override
	{
		if (effect == NULL)
			return;

		effect->control(effect.get(), VST_EFFECT_OPCODE_SUSPEND, 0, 1, NULL, 0.0f);
		effect->control(effect.get(), VST_EFFECT_OPCODE_PROCESS_BEGIN, 0, 0, NULL, 0.0f);
		refreshSampleWidths();
		vst2Processing = true;
	}

	void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount) override
	{
		if (effect == NULL)
			return;

		effect->process_double(effect.get(), inputArray, outputArray, frameCount);
	}

	void processReplacing(float** inputArray, float** outputArray, int frameCount) override
	{
		if (effect == NULL)
			return;

		effect->process_float(effect.get(), inputArray, outputArray, frameCount);
	}

	void process(float** inputArray, float** outputArray, int frameCount) override
	{
		if (effect == NULL)
			return;

		effect->process(effect.get(), inputArray, outputArray, frameCount);
	}

	void stopProcessing() override
	{
		if (effect == NULL)
			return;

		effect->control(effect.get(), VST_EFFECT_OPCODE_PROCESS_END, 0, 0, NULL, 0.0f);
		effect->control(effect.get(), VST_EFFECT_OPCODE_SUSPEND, 0, 0, NULL, 0.0f);
		vst2Processing = false;
	}

	bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor) override;

	void doIdle() override
	{
		if (effect == NULL)
			return;

		effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f);
	}

	void stopEditing() override
	{
		if (effect == NULL)
			return;

		effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, NULL, 0.0f);
	}

	// A VST2 editor sizes itself in the host frame's own units.
	void onSizeWindow(int w, int h) override { notifySizeWindow(w, h); }

	// Backing store for VST_HOST_OPCODE_GET_TIME, refreshed and returned per
	// call. Per instance: plugins in different audio streams process
	// concurrently, so a shared global here would let them race on one struct.
	vst_time_info* hostTimeInfo()
	{
		vstTime.sampleRate = getSampleRate();
		return &vstTime;
	}

private:
	// Audit #250 F040: the VST2 loader distinguishes its failure modes so
	// initialize() can log the actual reason (the old bool collapsed every
	// failure into "an exception").
	enum class LoadResult
	{
		Loaded,
		Crashed,
		NoEntryPoint,
		WrongMagicNumber
	};

	LoadResult load();

	// The engine asks for the sample width before every block. The effect's
	// flags are read here instead, on the control thread (after loading and
	// again before processing starts), so that query is a plain member read.
	void refreshSampleWidths()
	{
		// Without an effect the accessors have always answered float yes,
		// double no.
		floatProcessing = effect == NULL || (effect->flags & VST_EFFECT_FLAG_SUPPORTS_FLOAT) != 0;
		doubleProcessing = effect != NULL && (effect->flags & VST_EFFECT_FLAG_SUPPORTS_DOUBLE) != 0;
	}

	VST2EffectPtr effect;
	bool vst2Processing = false;
	vst_time_info vstTime{ 0,0,0,0,0,0,0,0,0,0,{0}, 0xFFFF };
};

intptr_t callback(struct vst_effect_t* effect, int32_t opcode, int32_t index, int64_t value, const char* ptr, float opt)
{
	VST2Instance* instance = effect != NULL ? (VST2Instance*)effect->host_internal : NULL;
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

VST2Instance::LoadResult VST2Instance::load()
{
	LoadResult result = LoadResult::Loaded;

	__try
	{
		vst_effect_t* candidate = library->VSTPluginMain(callback);
		if (candidate == NULL)
		{
			result = LoadResult::NoEntryPoint;
		}
		else if (candidate->magic_number != VST_MAGICNUMBER)
		{
			result = LoadResult::WrongMagicNumber;
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
		result = LoadResult::Crashed;
	}

	return result;
}

bool VST2Instance::initialize()
{
	// load() must stay free of objects requiring stack unwinding because it
	// uses a __try/__except guard (MSVC C2712), so log its failure reasons here where
	// constructing the std::wstring temporary from getLibPath is allowed.
	// Audit #250 F040: the loader now reports which of its three failure
	// modes happened instead of blaming every one on "an exception".
	const LoadResult result = load();
	refreshSampleWidths();
	switch (result)
	{
	case LoadResult::Loaded:
		return true;
	case LoadResult::Crashed:
		LogF(L"Loading VST2 plugin %s failed due to an exception.", library->getLibPath().c_str());
		return false;
	case LoadResult::NoEntryPoint:
		LogF(L"VST2 plugin %s returned no effect from its entry point, not loading it.", library->getLibPath().c_str());
		return false;
	case LoadResult::WrongMagicNumber:
	default:
		LogF(L"VST2 plugin %s has wrong magic number, not loading it.", library->getLibPath().c_str());
		return false;
	}
}

bool VST2Instance::startEditing(HWND hWnd, short* width, short* height, double)
{
	if (effect == NULL)
		return false;

	// An effect without its own editor has nothing to open; asking anyway
	// left rect uninitialised and dereferenced it (audit #348 TD-14).
	if ((effect->flags & VST_EFFECT_FLAG_EDITOR) == 0)
		return false;

	vst_rect_t* rect = nullptr;
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, hWnd, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);

	if (rect == nullptr || rect->right - rect->left <= 0 || rect->bottom - rect->top <= 0)
	{
		// Opened but unsized: close it again, since a false return means the
		// caller will not call stopEditing() for this window.
		effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, NULL, 0.0f);
		return false;
	}

	if (width != NULL)
		*width = rect->right - rect->left;
	if (height != NULL)
		*height = rect->bottom - rect->top;
	return true;
}

void VST2Instance::writeToEffect(const wstring& chunkData, const unordered_map<wstring, float>& paramMap)
{
	if (effect == NULL)
		return;

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		if (chunkData != L"")
		{
			vector<char> data;
			if (decodeBase64(chunkData, data))
				effect->control(effect.get(), VST_EFFECT_OPCODE_SET_CHUNK_DATA, 1, data.size(), data.data(), 0.0f);
		}
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = wintext::toWideString(buf, CP_UTF8);
			auto it = paramMap.find(name);
			if (it != paramMap.end())
				effect->set_parameter(effect.get(), i, it->second);
		}
	}
}

void VST2Instance::readFromEffect(wstring& chunkData, unordered_map<wstring, float>& paramMap) const
{
	if (effect == NULL)
		return;

	chunkData = L"";
	paramMap.clear();

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		BYTE* chunk = NULL;
		int size = (int)effect->control(effect.get(), VST_EFFECT_OPCODE_GET_CHUNK_DATA, 1, 0, &chunk, 0.0f);
		if (chunk != NULL && size > 0)
			encodeBase64(chunk, static_cast<size_t>(size), chunkData);
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			float value = effect->get_parameter(effect.get(), i);
			paramMap[wintext::toWideString(buf, CP_UTF8)] = value;
		}
	}
}
}

std::unique_ptr<VSTFormatInstance> createVST2Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
{
	return std::make_unique<VST2Instance>(library, processLevel);
}
