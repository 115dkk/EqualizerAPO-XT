/*
	This file is part of EqualizerAPO-XT.

	A minimal, self-contained VST 2.4 plugin built only against the project's
	vendored VST2 ABI header (helpers/aeffectx.h). It exists so VstHostTests
	(in Tests/HybridConvTests) can drive the engine's VST host classes
	(VSTPluginLibrary / VSTPluginInstance) against a real
	binary at runtime. Because it is compiled from our own source for each
	target, it always matches the host architecture - the load/state test runs
	on x64 and ARM64 without an architecture skip.

	Behaviour, kept deliberately deterministic so the host test can assert on it:
	  - 2 inputs / 2 outputs, 2 float parameters (param 0 = "Gain", param 1 =
	    "Bypass"). Both are plain [0,1] floats; the test treats Gain as a linear
	    multiplier.
	  - processReplacing / processDoubleReplacing copy input to output scaled by
	    Gain, unless Bypass >= 0.5 in which case input is passed through unscaled.
	  - flags advertise canReplacing (float), canDoubleReplacing (double) and
	    programChunks (chunk-based state).
	  - The chunk is a fixed little-endian blob: a 4-byte magic 'TVP2', a uint32
	    version, then the two float parameters. getChunk emits it; setChunk
	    restores the parameters from it. This lets the host round-trip parameter
	    state through readFromEffect/writeToEffect, which (per the engine) use the
	    chunk path whenever the programChunks flag is set.

	No engine headers, no Qt, no CRT surprises: just <cstring>/<cstdint> and the
	ABI header. The exported entry point is VSTPluginMain (the symbol
	VSTPluginLibrary::loadFunctions resolves), with a legacy `main` alias for
	hosts that fall back to it.
*/

#include <cstdint>
#include <cstring>

#include "helpers/aeffectx.h"

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
struct PluginState
{
	float params[kNumParams];
	ChunkBlob chunkScratch;
};

PluginState* stateOf(vst_effect_t* effect)
{
	return effect != nullptr ? static_cast<PluginState*>(effect->effect_internal) : nullptr;
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
	(void)value;
	(void)opt;
	PluginState* state = stateOf(effect);

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
		*static_cast<void**>(ptr) = &state->chunkScratch;
		return static_cast<intptr_t>(sizeof(ChunkBlob));
	}

	case VST_EFFECT_OPCODE_SET_CHUNK_DATA: // effSetChunk
	{
		if (state == nullptr || ptr == nullptr || value < static_cast<intptr_t>(sizeof(ChunkBlob)))
			return 0;
		ChunkBlob blob;
		std::memcpy(&blob, ptr, sizeof(ChunkBlob));
		if (blob.magic != kChunkMagic)
			return 0;
		state->params[kParamGain] = blob.gain;
		state->params[kParamBypass] = blob.bypass;
		return 1;
	}

	// Editor / GUI opcodes: this plugin has no editor.
	case VST_EFFECT_OPCODE_EDITOR_GET_RECT:
	case VST_EFFECT_OPCODE_EDITOR_OPEN:
	case VST_EFFECT_OPCODE_EDITOR_CLOSE:
		return 0;

	default:
		return 0;
	}
}

vst_effect_t* createEffect()
{
	PluginState* state = new PluginState();
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

	return effect;
}
} // namespace

// VST 2.x entry point. VSTPluginLibrary::loadFunctions resolves exactly this
// symbol via GetProcAddress(module, "VSTPluginMain"); the callback lets the
// plugin query the host but this plugin needs nothing from it, so we ignore it
// after the create.
//
// The engine's loader only ever looks up "VSTPluginMain", never the legacy
// `main`/`MAIN` aliases, so no alias export is added here. Adding one would not
// be reached by this host and exporting a function literally named `main` from
// a DLL is needless surface area.
extern "C" __declspec(dllexport) vst_effect_t* VST_FUNCTION_INTERFACE VSTPluginMain(vst_host_callback_t callback)
{
	(void)callback;
	return createEffect();
}
