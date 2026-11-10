/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	A minimal, self-contained VST 2.4 plugin that also exports a zero-class
	IPluginFactory3. Vendors such as Kilohearts ship the same dual-export binary
	as their VST2 .dll product, so VstHostTests uses this fixture to keep the
	engine's format-selection rule covered as well as driving VSTPluginLibrary /
	VSTPluginInstance against a real binary at runtime. Because it is compiled
	from our own source for each target, it always matches the host architecture
	- the load/state test runs on x64 and ARM64 without an architecture skip.

	Behaviour, kept deliberately deterministic so the host test can assert on it:
	  - 2 inputs / 2 outputs, 2 float parameters (param 0 = "Gain", param 1 =
	    "Bypass"). Both are plain [0,1] floats; the test treats Gain as a linear
	    multiplier.
	  - processReplacing / processDoubleReplacing copy input to output scaled by
	    Gain, unless Bypass >= 0.5 in which case input is passed through unscaled.
	  - flags advertise canReplacing (float), canDoubleReplacing (double) and
	    programChunks (chunk-based state). EAPO_TEST_VST_EDITOR selects a fixed,
	    DPI-scaled or late-scaling editor persona; unset keeps the plugin editorless.
	  - The normal chunk is a fixed little-endian blob: a 4-byte magic 'TVP2', a
	    uint32 version, then the two float parameters. EAPO_TEST_VST_STATE selects
	    a ZIP-clock persona that rewrites its ZIP timestamps on every read, with an
	    optional automate callback during setChunk, like the Kilohearts plugins.
	    This lets the host round-trip parameter state through
	    readFromEffect/writeToEffect, which use the chunk path whenever the
	    programChunks flag is set.
	  - EapoTestVst2MoveGain changes every live fixture instance and can send the
	    matching automate callback, so an Editor gate can distinguish a real state
	    change from a rewritten archive.

	A copy whose file name contains SlowLoad followed immediately by decimal
	digits becomes the loader-lock timing persona. For example,
	TestVst2Plugin.SlowLoad4000.dll sleeps for 4 seconds while loading. The
	ordinary TestVst2Plugin.dll has no added delay.

	No engine headers and no Qt are used. The module exports VSTPluginMain for
	its working VST2 product and GetPluginFactory for the deliberately unusable
	VST3 side of the dual-export fixture.
*/

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "vst/aeffectx.h"

#define INIT_CLASS_IID
#include "pluginterfaces/base/ipluginbase.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		wchar_t path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(hModule, path, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
		{
			const wchar_t* fileName = wcsrchr(path, L'\\');
			fileName = fileName != nullptr ? fileName + 1 : path;
			const wchar_t* marker = fileName;
			while ((marker = wcsstr(marker, L"SlowLoad")) != nullptr)
			{
				marker += 8;
				if (*marker < L'0' || *marker > L'9')
					continue;
				DWORD milliseconds = 0;
				while (*marker >= L'0' && *marker <= L'9')
				{
					const DWORD digit = static_cast<DWORD>(*marker - L'0');
					milliseconds = milliseconds > (60000 - digit) / 10 ? 60000 : milliseconds * 10 + digit;
					marker++;
				}
				// This sleep deliberately holds the loader lock to imitate a plugin
				// whose process-attach work blocks every loader-dependent thread.
				Sleep(milliseconds);
				break;
			}
		}
	}
	return TRUE;
}

namespace
{
// Identity for this test plugin. 'EXT2' keeps it clearly distinct from the
// engine's own 'EAPO' VST id.
const int32_t kUniqueId = VST_FOURCC('E', 'X', 'T', '2');
const int32_t kNumInputs = 2;
const int32_t kNumOutputs = 2;
const int32_t kNumParams = 2;

const int32_t kParamGain = 0;
const int32_t kParamBypass = 1;

// Chunk blob layout: magic + version + the two float parameters, all
// little-endian. The host stores this base64-encoded, so its exact bytes must
// be stable for the round-trip assertion.
const uint32_t kChunkMagic = 0x32505654u; // 'TVP2' little-endian ('T','V','P','2')
const uint32_t kChunkVersion = 1u;
const size_t kZipChunkSize = 148;
const size_t kZipBlobOffset = 39;

#pragma pack(push, 1)
struct ChunkBlob
{
	uint32_t magic;
	uint32_t version;
	float gain;
	float bypass;
};
#pragma pack(pop)

// Per-instance state. We allocate one and stash it in effect->effect_internal
// so a host may (in principle) create more than one instance. The chunk buffer
// lives here too because effGetChunk hands the host a pointer it reads after we
// return.
enum class EditorPersona
{
	none,
	fixed,
	scaled,
	late
};

enum class StatePersona
{
	plain,
	zipClock,
	zipClockAutomate
};

struct PluginState
{
	vst_host_callback_t host = nullptr;
	float params[kNumParams]{};
	ChunkBlob chunkScratch{};
	std::array<uint8_t, kZipChunkSize> zipChunkScratch{};
	uint16_t zipTime = 0;
	vst_rect_t rect{};
	EditorPersona editorPersona = EditorPersona::none;
	StatePersona statePersona = StatePersona::plain;
	double scale = 1.0;
	bool rectQueried = false;
	bool editorOpen = false;
	bool dispatcherActive = false;
	bool editorFailed = false;
};

PluginState* stateOf(vst_effect_t* effect)
{
	return effect != nullptr ? static_cast<PluginState*>(effect->effect_internal) : nullptr;
}

SRWLOCK liveEffectsLock = SRWLOCK_INIT;
std::array<vst_effect_t*, 32> liveEffects{};

void registerLiveEffect(vst_effect_t* effect)
{
	AcquireSRWLockExclusive(&liveEffectsLock);
	for (vst_effect_t*& entry : liveEffects)
	{
		if (entry == nullptr)
		{
			entry = effect;
			break;
		}
	}
	ReleaseSRWLockExclusive(&liveEffectsLock);
}

void unregisterLiveEffect(const vst_effect_t* effect)
{
	AcquireSRWLockExclusive(&liveEffectsLock);
	for (vst_effect_t*& entry : liveEffects)
	{
		if (entry == effect)
		{
			entry = nullptr;
			break;
		}
	}
	ReleaseSRWLockExclusive(&liveEffectsLock);
}

void moveGainForLiveEffects(float gain, bool automate)
{
	struct CallbackTarget
	{
		vst_effect_t* effect = nullptr;
		vst_host_callback_t host = nullptr;
	};
	std::array<CallbackTarget, 32> callbacks{};
	size_t callbackCount = 0;

	AcquireSRWLockExclusive(&liveEffectsLock);
	for (vst_effect_t* effect : liveEffects)
	{
		PluginState* state = stateOf(effect);
		if (state == nullptr)
			continue;
		state->params[kParamGain] = gain;
		if (automate && state->host != nullptr)
			callbacks[callbackCount++] = { effect, state->host };
	}
	ReleaseSRWLockExclusive(&liveEffectsLock);

	// Host callbacks can re-enter or destroy the fixture, so the registry lock
	// must not be held here.
	for (size_t i = 0; i < callbackCount; ++i)
		callbacks[i].host(callbacks[i].effect, VST_HOST_OPCODE_AUTOMATE,
			kParamGain, 0, nullptr, gain);
}

void writeU16(std::array<uint8_t, kZipChunkSize>& buffer, size_t offset, uint16_t value)
{
	buffer[offset] = static_cast<uint8_t>(value);
	buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(std::array<uint8_t, kZipChunkSize>& buffer, size_t offset, uint32_t value)
{
	buffer[offset] = static_cast<uint8_t>(value);
	buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
	buffer[offset + 2] = static_cast<uint8_t>(value >> 16);
	buffer[offset + 3] = static_cast<uint8_t>(value >> 24);
}

uint32_t crc32(const uint8_t* data, size_t size)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; ++i)
	{
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0xEDB88320u : 0u);
	}
	return ~crc;
}

void buildZipChunk(PluginState* state)
{
	std::array<uint8_t, kZipChunkSize>& buffer = state->zipChunkScratch;
	buffer.fill(0);
	const uint16_t time = ++state->zipTime;
	const uint16_t date = 0x5D6Au;
	const uint8_t* blob = reinterpret_cast<const uint8_t*>(&state->chunkScratch);
	const uint32_t crc = crc32(blob, sizeof(ChunkBlob));
	const char name[] = "state.bin";

	writeU32(buffer, 0, 0x04034B50u);
	writeU16(buffer, 4, 20);
	writeU16(buffer, 6, 0x0808);
	writeU16(buffer, 8, 0);
	writeU16(buffer, 10, time);
	writeU16(buffer, 12, date);
	writeU32(buffer, 14, 0);
	writeU32(buffer, 18, 0);
	writeU32(buffer, 22, 0);
	writeU16(buffer, 26, 9);
	writeU16(buffer, 28, 0);
	std::memcpy(buffer.data() + 30, name, 9);
	std::memcpy(buffer.data() + kZipBlobOffset, blob, sizeof(ChunkBlob));

	writeU32(buffer, 55, 0x08074B50u);
	writeU32(buffer, 59, crc);
	writeU32(buffer, 63, static_cast<uint32_t>(sizeof(ChunkBlob)));
	writeU32(buffer, 67, static_cast<uint32_t>(sizeof(ChunkBlob)));

	writeU32(buffer, 71, 0x02014B50u);
	writeU16(buffer, 75, 20);
	writeU16(buffer, 77, 20);
	writeU16(buffer, 79, 0x0808);
	writeU16(buffer, 81, 0);
	writeU16(buffer, 83, time);
	writeU16(buffer, 85, date);
	writeU32(buffer, 87, crc);
	writeU32(buffer, 91, static_cast<uint32_t>(sizeof(ChunkBlob)));
	writeU32(buffer, 95, static_cast<uint32_t>(sizeof(ChunkBlob)));
	writeU16(buffer, 99, 9);
	writeU16(buffer, 101, 0);
	writeU16(buffer, 103, 0);
	writeU16(buffer, 105, 0);
	writeU16(buffer, 107, 0);
	writeU32(buffer, 109, 0);
	writeU32(buffer, 113, 0);
	std::memcpy(buffer.data() + 117, name, 9);

	writeU32(buffer, 126, 0x06054B50u);
	writeU16(buffer, 130, 0);
	writeU16(buffer, 132, 0);
	writeU16(buffer, 134, 1);
	writeU16(buffer, 136, 1);
	writeU32(buffer, 138, 55);
	writeU32(buffer, 142, 71);
	writeU16(buffer, 146, 0);
}

void VST_FUNCTION_INTERFACE setParameter(vst_effect_t* effect, uint32_t index, float value)
{
	PluginState* state = stateOf(effect);
	if (state == nullptr || index >= static_cast<uint32_t>(kNumParams))
		return;
	state->params[index] = value;
}

float VST_FUNCTION_INTERFACE getParameter(vst_effect_t* effect, uint32_t index)
{
	const PluginState* state = stateOf(effect);
	if (state == nullptr || index >= static_cast<uint32_t>(kNumParams))
		return 0.0f;
	return state->params[index];
}

// out[n] = in[n] * gain, or a straight copy when bypassed. Shared by the float
// and double process callbacks via the template so the math is identical.
template<typename Sample>
void processGeneric(vst_effect_t* effect, const Sample* const* inputs, Sample** outputs, int32_t samples)
{
	const PluginState* state = stateOf(effect);
	const double gain = state != nullptr ? static_cast<double>(state->params[kParamGain]) : 1.0;
	const bool bypass = state != nullptr && state->params[kParamBypass] >= 0.5f;

	for (int32_t ch = 0; ch < kNumOutputs; ++ch)
	{
		const Sample* in = inputs[ch];
		Sample* out = outputs[ch];
		if (in == nullptr || out == nullptr)
			continue;

		if (bypass)
		{
			for (int32_t i = 0; i < samples; ++i)
				out[i] = in[i];
		}
		else
		{
			for (int32_t i = 0; i < samples; ++i)
				out[i] = static_cast<Sample>(in[i] * gain);
		}
	}
}

void VST_FUNCTION_INTERFACE processReplacing(vst_effect_t* effect, const float* const* inputs, float** outputs, int32_t samples)
{
	processGeneric<float>(effect, inputs, outputs, samples);
}

void VST_FUNCTION_INTERFACE processDoubleReplacing(vst_effect_t* effect, const double* const* inputs, double** outputs, int32_t samples)
{
	processGeneric<double>(effect, inputs, outputs, samples);
}

// Deprecated accumulate-style process. The host (and our test) use the
// replacing variants, but we point it at the same math for completeness.
void VST_FUNCTION_INTERFACE processAccumulating(vst_effect_t* effect, const float* const* inputs, float** outputs, int32_t samples)
{
	processGeneric<float>(effect, inputs, outputs, samples);
}

void copyName(void* dst, const char* text, size_t bufSize)
{
	if (dst == nullptr)
		return;
	char* out = static_cast<char*>(dst);
	size_t i = 0;
	for (; text[i] != '\0' && i + 1 < bufSize; ++i)
		out[i] = text[i];
	out[i] = '\0';
}

intptr_t VST_FUNCTION_INTERFACE dispatcher(vst_effect_t* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
{
	PluginState* state = stateOf(effect);
	if (state != nullptr && state->dispatcherActive)
	{
		state->editorFailed = true;
		return 0;
	}

	switch (opcode)
	{
	case VST_EFFECT_OPCODE_INITIALIZE: // effOpen
	case VST_EFFECT_OPCODE_SET_SAMPLE_RATE:
	case VST_EFFECT_OPCODE_SET_BLOCK_SIZE:
	case VST_EFFECT_OPCODE_SUSPEND: // mainsChanged (resume/suspend)
	case VST_EFFECT_OPCODE_PROCESS_BEGIN:
	case VST_EFFECT_OPCODE_PROCESS_END:
		return 0;

	case VST_EFFECT_OPCODE_DESTROY: // effClose
		unregisterLiveEffect(effect);
		// The host must detach caller closures before entering plug-in teardown.
		if (state != nullptr && state->host != nullptr)
		{
			state->host(effect, VST_HOST_OPCODE_AUTOMATE, 0, 0, nullptr, 0.0f);
			state->host(effect, VST_HOST_OPCODE_EDITOR_RESIZE, 400, 300, nullptr, 0.0f);
		}
		delete state;
		if (effect != nullptr)
			effect->effect_internal = nullptr;
		// The effect struct itself was allocated in VSTPluginMain.
		delete effect;
		return 0;

	case VST_EFFECT_OPCODE_VST_VERSION:
		return VST_VERSION_2_4_0_0;

	case VST_EFFECT_OPCODE_CATEGORY:
		return VST_EFFECT_CATEGORY_EFFECT;

	case VST_EFFECT_OPCODE_EFFECT_NAME:
		copyName(ptr, "TestVst2Plugin", VST_BUFFER_SIZE_EFFECT_NAME);
		return 1;

	case VST_EFFECT_OPCODE_VENDOR_NAME:
		copyName(ptr, "EqualizerAPO-XT Tests", VST_BUFFER_SIZE_VENDOR_NAME);
		return 1;

	case VST_EFFECT_OPCODE_PRODUCT_NAME:
		copyName(ptr, "TestVst2Plugin", VST_BUFFER_SIZE_PRODUCT_NAME);
		return 1;

	case VST_EFFECT_OPCODE_VENDOR_VERSION:
		return 1;

	case VST_EFFECT_OPCODE_PARAM_GET_NAME:
		if (index == kParamGain)
			copyName(ptr, "Gain", VST_BUFFER_SIZE_PARAM_NAME);
		else if (index == kParamBypass)
			copyName(ptr, "Bypass", VST_BUFFER_SIZE_PARAM_NAME);
		else
			copyName(ptr, "", VST_BUFFER_SIZE_PARAM_NAME);
		return 0;

	case VST_EFFECT_OPCODE_PARAM_GET_LABEL:
		copyName(ptr, "", VST_BUFFER_SIZE_PARAM_LABEL);
		return 0;

	case VST_EFFECT_OPCODE_PARAM_GET_VALUE:
		copyName(ptr, "", VST_BUFFER_SIZE_PARAM_VALUE);
		return 0;

	case VST_EFFECT_OPCODE_PARAM_IS_AUTOMATABLE:
		return 1;

	case VST_EFFECT_OPCODE_SUPPORTS: // effCanDo
	{
		const char* feature = static_cast<const char*>(ptr);
		if (feature != nullptr && std::strcmp(feature, "bypass") == 0)
			return 1;
		if (feature != nullptr && std::strcmp(feature, vst_effect_supports.supportsViewDpiScaling) == 0)
			return state != nullptr && (state->editorPersona == EditorPersona::scaled
				|| state->editorPersona == EditorPersona::late) ? 1 : 0;
		return 0;
	}

	case VST_EFFECT_OPCODE_GET_CHUNK_DATA: // effGetChunk
	{
		if (state == nullptr || ptr == nullptr)
			return 0;
		state->chunkScratch.magic = kChunkMagic;
		state->chunkScratch.version = kChunkVersion;
		state->chunkScratch.gain = state->params[kParamGain];
		state->chunkScratch.bypass = state->params[kParamBypass];
		if (state->statePersona == StatePersona::plain)
		{
			*static_cast<void**>(ptr) = &state->chunkScratch;
			return static_cast<intptr_t>(sizeof(ChunkBlob));
		}
		buildZipChunk(state);
		*static_cast<void**>(ptr) = state->zipChunkScratch.data();
		return static_cast<intptr_t>(kZipChunkSize);
	}

	case VST_EFFECT_OPCODE_SET_CHUNK_DATA: // effSetChunk
	{
		if (state == nullptr || ptr == nullptr)
			return 0;
		const void* blobData = ptr;
		if (state->statePersona == StatePersona::plain)
		{
			if (value < static_cast<intptr_t>(sizeof(ChunkBlob)))
				return 0;
		}
		else if (value == static_cast<intptr_t>(kZipChunkSize))
			blobData = static_cast<const uint8_t*>(ptr) + kZipBlobOffset;
		else if (value != static_cast<intptr_t>(sizeof(ChunkBlob)))
			return 0;

		ChunkBlob blob;
		std::memcpy(&blob, blobData, sizeof(ChunkBlob));
		if (blob.magic != kChunkMagic)
			return 0;
		state->params[kParamGain] = blob.gain;
		state->params[kParamBypass] = blob.bypass;
		if (state->statePersona == StatePersona::zipClockAutomate && state->host != nullptr)
		{
			state->host(effect, VST_HOST_OPCODE_AUTOMATE, kParamGain, 0, nullptr, blob.gain);
			state->host(effect, VST_HOST_OPCODE_AUTOMATE, kParamGain, 0, nullptr, blob.gain);
		}
		return 1;
	}

	case VST_EFFECT_OPCODE_CUSTOM:
		if (state != nullptr && index == VST_EFFECT_CONTENT_SCALE_INDEX
			&& value == VST_EFFECT_CONTENT_SCALE_VALUE)
		{
			if (state->editorPersona == EditorPersona::fixed)
			{
				state->editorFailed = true;
				return 0;
			}
			if (state->editorPersona == EditorPersona::late && !state->editorOpen)
				return 1;
			if ((state->editorPersona == EditorPersona::scaled
				|| state->editorPersona == EditorPersona::late)
				&& (!state->rectQueried || state->editorOpen))
			{
				const short oldWidth = static_cast<short>(state->rect.right - state->rect.left);
				const short oldHeight = static_cast<short>(state->rect.bottom - state->rect.top);
				const double oldScale = state->scale;
				state->scale = opt;
				const short newWidth = static_cast<short>(std::lround(320.0 * state->scale));
				const short newHeight = static_cast<short>(std::lround(200.0 * state->scale));
				if (state->editorOpen && state->host != nullptr)
				{
					state->dispatcherActive = true;
					const intptr_t accepted = state->host(effect, VST_HOST_OPCODE_EDITOR_RESIZE,
						newWidth, newHeight, nullptr, 0.0f);
					state->dispatcherActive = false;
					if (accepted != 1)
					{
						state->scale = oldScale;
						state->rect.right = oldWidth;
						state->rect.bottom = oldHeight;
						return 0;
					}
				}
				state->rect.right = newWidth;
				state->rect.bottom = newHeight;
			}
		}
		return 0;

	case VST_EFFECT_OPCODE_EDITOR_GET_RECT:
		if (state == nullptr || ptr == nullptr || state->editorPersona == EditorPersona::none)
			return 0;
		state->rect.left = 0;
		state->rect.top = 0;
		if (state->editorFailed)
		{
			state->rect.right = 1;
			state->rect.bottom = 1;
		}
		else if (state->editorPersona == EditorPersona::fixed)
		{
			state->rect.right = 320;
			state->rect.bottom = 200;
		}
		else
		{
			state->rect.right = static_cast<short>(std::lround(320.0 * state->scale));
			state->rect.bottom = static_cast<short>(std::lround(200.0 * state->scale));
		}
		*static_cast<vst_rect_t**>(ptr) = &state->rect;
		state->rectQueried = true;
		return 1;

	case VST_EFFECT_OPCODE_EDITOR_OPEN:
		if (state == nullptr || state->editorPersona == EditorPersona::none)
			return 0;
		state->editorOpen = true;
		if ((state->editorPersona == EditorPersona::scaled
			|| state->editorPersona == EditorPersona::late) && state->host != nullptr)
		{
			state->dispatcherActive = true;
			const intptr_t modified = state->host(effect, VST_HOST_OPCODE_IO_MODIFIED, 0, 0, nullptr, 0.0f);
			const intptr_t idle = state->host(effect, VST_HOST_OPCODE_IO_NEED_IDLE, 0, 0, nullptr, 0.0f);
			state->dispatcherActive = false;
			if (modified != 0 || idle != 0)
				state->editorFailed = true;
		}
		return 1;

	case VST_EFFECT_OPCODE_EDITOR_CLOSE:
		if (state != nullptr)
		{
			state->editorOpen = false;
			state->rectQueried = false;
		}
		return 0;

	default:
		return 0;
	}
}

vst_effect_t* createEffect(vst_host_callback_t callback)
{
	PluginState* state = new PluginState();
	state->host = callback;
	state->params[kParamGain] = 1.0f;
	state->params[kParamBypass] = 0.0f;
	std::memset(&state->chunkScratch, 0, sizeof(state->chunkScratch));

	vst_effect_t* effect = new vst_effect_t();
	std::memset(effect, 0, sizeof(*effect));

	effect->magic_number = VST_MAGICNUMBER;
	effect->control = &dispatcher;
	effect->process = &processAccumulating;
	effect->set_parameter = &setParameter;
	effect->get_parameter = &getParameter;
	effect->num_programs = 1;
	effect->num_params = kNumParams;
	effect->num_inputs = kNumInputs;
	effect->num_outputs = kNumOutputs;
	effect->flags = VST_EFFECT_FLAG_SUPPORTS_FLOAT
		| VST_EFFECT_FLAG_SUPPORTS_DOUBLE
		| VST_EFFECT_FLAG_CHUNKS;
	effect->delay = 0;
	effect->input_output_ratio = 1.0f;
	effect->effect_internal = state;
	effect->host_internal = nullptr;
	effect->unique_id = kUniqueId;
	effect->version = 1;
	effect->process_float = &processReplacing;
	effect->process_double = &processDoubleReplacing;

	wchar_t editorMode[32] = {};
	if (GetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", editorMode,
		static_cast<DWORD>(sizeof(editorMode) / sizeof(editorMode[0]))) > 0)
	{
		if (wcscmp(editorMode, L"fixed") == 0)
			state->editorPersona = EditorPersona::fixed;
		else if (wcscmp(editorMode, L"scaled") == 0)
			state->editorPersona = EditorPersona::scaled;
		else if (wcscmp(editorMode, L"late") == 0)
			state->editorPersona = EditorPersona::late;
		if (state->editorPersona != EditorPersona::none)
			effect->flags |= VST_EFFECT_FLAG_EDITOR;
	}

	wchar_t stateMode[32] = {};
	if (GetEnvironmentVariableW(L"EAPO_TEST_VST_STATE", stateMode,
		static_cast<DWORD>(sizeof(stateMode) / sizeof(stateMode[0]))) > 0)
	{
		if (wcscmp(stateMode, L"zip-clock") == 0)
			state->statePersona = StatePersona::zipClock;
		else if (wcscmp(stateMode, L"zip-clock-automate") == 0)
			state->statePersona = StatePersona::zipClockAutomate;
	}

	// Host robustness tests select malformed metadata before creating an
	// instance. Normal test runs leave the variable unset and retain the fixed
	// two-channel contract documented above.
	wchar_t metadataMode[32] = {};
	if (GetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", metadataMode,
		static_cast<DWORD>(sizeof(metadataMode) / sizeof(metadataMode[0]))) > 0)
	{
		if (wcscmp(metadataMode, L"huge-inputs") == 0)
			effect->num_inputs = 2048;
		else if (wcscmp(metadataMode, L"negative-inputs") == 0)
			effect->num_inputs = -1;
		else if (wcscmp(metadataMode, L"huge-outputs") == 0)
			effect->num_outputs = 2048;
		else if (wcscmp(metadataMode, L"negative-outputs") == 0)
			effect->num_outputs = -1;
		else if (wcscmp(metadataMode, L"negative-delay") == 0)
			effect->delay = -1;
		else if (wcscmp(metadataMode, L"huge-delay") == 0)
			effect->delay = INT32_MAX;
		else if (wcscmp(metadataMode, L"latency-512") == 0)
			effect->delay = 512;
	}

	registerLiveEffect(effect);
	return effect;
}

// Deliberately advertises no VST3 classes and refuses host-context setup, as
// Kilohearts' dual-export VST2 DLL does. Selecting this factory instead of
// VSTPluginMain therefore makes the fixture fail at library initialization.
class EmptyVst3Factory final : public Steinberg::IPluginFactory3
{
public:
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return Steinberg::kInvalidArgument;
		if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)
			|| Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPluginFactory::iid))
			*obj = static_cast<Steinberg::IPluginFactory*>(this);
		else if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPluginFactory2::iid))
			*obj = static_cast<Steinberg::IPluginFactory2*>(this);
		else if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPluginFactory3::iid))
			*obj = static_cast<Steinberg::IPluginFactory3*>(this);
		else
		{
			*obj = nullptr;
			return Steinberg::kNoInterface;
		}
		addRef();
		return Steinberg::kResultOk;
	}

	Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount; }
	Steinberg::uint32 PLUGIN_API release() override
	{
		const Steinberg::uint32 remaining = --refCount;
		if (remaining == 0)
			delete this;
		return remaining;
	}

	Steinberg::tresult PLUGIN_API getFactoryInfo(Steinberg::PFactoryInfo* info) override
	{
		if (info == nullptr)
			return Steinberg::kInvalidArgument;
		*info = Steinberg::PFactoryInfo("EqualizerAPO-XT Tests", "https://example.invalid",
			"test@example.invalid", Steinberg::PFactoryInfo::kNoFlags);
		return Steinberg::kResultOk;
	}
	Steinberg::int32 PLUGIN_API countClasses() override { return 0; }
	Steinberg::tresult PLUGIN_API getClassInfo(Steinberg::int32, Steinberg::PClassInfo*) override
	{
		return Steinberg::kInvalidArgument;
	}
	Steinberg::tresult PLUGIN_API createInstance(Steinberg::FIDString, Steinberg::FIDString, void** obj) override
	{
		if (obj != nullptr)
			*obj = nullptr;
		return Steinberg::kNotImplemented;
	}
	Steinberg::tresult PLUGIN_API getClassInfo2(Steinberg::int32, Steinberg::PClassInfo2*) override
	{
		return Steinberg::kInvalidArgument;
	}
	Steinberg::tresult PLUGIN_API getClassInfoUnicode(Steinberg::int32, Steinberg::PClassInfoW*) override
	{
		return Steinberg::kInvalidArgument;
	}
	Steinberg::tresult PLUGIN_API setHostContext(Steinberg::FUnknown*) override
	{
		return Steinberg::kNotImplemented;
	}

private:
	~EmptyVst3Factory() = default;
	std::atomic<Steinberg::uint32> refCount{1};
};
} // namespace

extern "C" __declspec(dllexport) void EapoTestVst2MoveGain(float gain, int automate)
{
	moveGainForLiveEffects(gain, automate != 0);
}

// VST 2.x entry point. VSTPluginLibrary::loadFunctions resolves exactly this
// symbol via GetProcAddress(module, "VSTPluginMain"); the callback lets the
// plugin query the host, including during teardown.
//
// The dual-export regression fixture also exposes GetPluginFactory below. For
// a .dll, the host must still choose this working VSTPluginMain entry point.
// The legacy `main`/`MAIN` aliases are not part of the host's lookup contract.
extern "C" __declspec(dllexport) vst_effect_t* VST_FUNCTION_INTERFACE VSTPluginMain(vst_host_callback_t callback)
{
	wchar_t probe[2] = {};
	if (GetEnvironmentVariableW(L"EAPO_TEST_VST_HOST_CALLBACKS", probe, 2) != 0)
	{
		char product[64] = {};
		if (callback(nullptr, VST_HOST_OPCODE_PRODUCT_NAME, 0, 0, nullptr, 0.0f) != 0
			|| callback(nullptr, VST_HOST_OPCODE_PRODUCT_NAME, 0, 0, product, 0.0f) != 1
			|| strcmp(product, "Equalizer APO") != 0
			|| callback(nullptr, VST_HOST_OPCODE_SUPPORTS, 0, 0, nullptr, 0.0f) != 0
			|| callback(nullptr, VST_HOST_OPCODE_SUPPORTS, 0, 0, "sizeWindow", 0.0f) != 1)
			return nullptr;
	}
	return createEffect(callback);
}

extern "C" __declspec(dllexport) Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory()
{
	return new EmptyVst3Factory();
}
