/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)
*/

#include "PanelPreviewFeeder.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <vector>

#include "platform/windows/ComPtr.h"
#include "services/logging/Logging.h"
#include "vst/VSTPluginInstance.h"

using winutil::ComPtr;
using winutil::CoTaskMem;

namespace
{
// Capture packets are chopped into blocks of at most this many frames - the
// same bound handed to prepareForProcessing, which freezes the VST3
// maxSamplesPerBlock for the whole session (it may not change while the
// component is active). It doubles as the per-tick processing cap below.
constexpr UINT32 maxBlockFrames = 8192;
// 500 ms of capture headroom in 100-ns units, so short event-loop stalls
// lose no audio.
constexpr REFERENCE_TIME captureBufferDuration = 5000000;
constexpr int pumpIntervalMs = 30;

bool isFloat32Format(const WAVEFORMATEX* format)
{
	if (format->wBitsPerSample != 32)
		return false;
	if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
		return true;
	if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		const WAVEFORMATEXTENSIBLE* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
		return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
	}
	return false;
}

// The sample width of the preview process calls. prepareForProcessing
// declares kSample64 to a double-capable VST3 processor, so the feed must
// hand over the width the setup promised; the float paths cover the rest.
enum class ProcessWidth
{
	// processDoubleReplacing: double-capable VST3 and VST2 effects.
	Double64,
	// processReplacing: float-only processors.
	Float32,
	// VST2 process(), which accumulates into the output; the scratch is
	// zeroed before every call so accumulation equals replacement.
	Float32Accumulate
};

// The planar block buffers for one sample width. Only the width the session
// picked is ever allocated.
template<typename SampleType>
struct PlanarBuffers
{
	std::vector<std::vector<SampleType>> input;
	std::vector<std::vector<SampleType>> outputScratch;
	std::vector<SampleType*> inputPointers;
	std::vector<SampleType*> outputPointers;

	void allocate(int inputChannelCount, int outputChannelCount)
	{
		input.assign(inputChannelCount, std::vector<SampleType>(maxBlockFrames, SampleType(0)));
		outputScratch.assign(outputChannelCount, std::vector<SampleType>(maxBlockFrames, SampleType(0)));
		inputPointers.resize(inputChannelCount);
		outputPointers.resize(outputChannelCount);
		for (int channel = 0; channel < inputChannelCount; channel++)
			inputPointers[channel] = input[channel].data();
		for (int channel = 0; channel < outputChannelCount; channel++)
			outputPointers[channel] = outputScratch[channel].data();
	}

	// De-interleaves one chunk of the float32 capture stream. A plug-in
	// wider than the mix repeats the last capture channel; the usual case is
	// stereo onto stereo.
	void convertInput(const float* interleaved, UINT32 offset, UINT32 chunk,
		UINT32 captureChannelCount, bool silent)
	{
		for (size_t channel = 0; channel < input.size(); channel++)
		{
			SampleType* planar = input[channel].data();
			if (silent || captureChannelCount == 0)
			{
				std::fill_n(planar, chunk, SampleType(0));
				continue;
			}
			const UINT32 sourceChannel = std::min(static_cast<UINT32>(channel), captureChannelCount - 1);
			for (UINT32 i = 0; i < chunk; i++)
				planar[i] = static_cast<SampleType>(interleaved[(offset + i) * captureChannelCount + sourceChannel]);
		}
	}

	void zeroOutput()
	{
		for (std::vector<SampleType>& channel : outputScratch)
			std::fill(channel.begin(), channel.end(), SampleType(0));
	}
};

// The one call into third-party processing code, isolated so a crashing
// plug-in freezes its meters instead of taking the Editor down. Kept free of
// objects requiring stack unwinding because of the __try guard (MSVC C2712).
bool processGuarded(VSTPluginInstance* effect, ProcessWidth width, float** inputPointers,
	float** outputPointers, double** inputPointersDouble, double** outputPointersDouble,
	int frameCount) noexcept
{
	__try
	{
		switch (width)
		{
		case ProcessWidth::Double64:
			effect->processDoubleReplacing(inputPointersDouble, outputPointersDouble, frameCount);
			break;
		case ProcessWidth::Float32:
			effect->processReplacing(inputPointers, outputPointers, frameCount);
			break;
		case ProcessWidth::Float32Accumulate:
			effect->process(inputPointers, outputPointers, frameCount);
			break;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}
}

struct PanelPreviewFeeder::CaptureState
{
	VSTPluginInstance* effect = nullptr;
	bool isVst3 = false;
	ProcessWidth width = ProcessWidth::Float32;
	bool ownsVst2ProcessingState = false;
	ComPtr<IAudioClient> audioClient;
	ComPtr<IAudioCaptureClient> captureClient;
	CoTaskMem<WAVEFORMATEX> mixFormat;
	PlanarBuffers<float> floatBuffers;
	PlanarBuffers<double> doubleBuffers;
};

PanelPreviewFeeder::PanelPreviewFeeder()
{
	pumpTimer.setInterval(pumpIntervalMs);
	connect(&pumpTimer, &QTimer::timeout, this, &PanelPreviewFeeder::pump);
}

PanelPreviewFeeder::~PanelPreviewFeeder()
{
	stop();
}

void PanelPreviewFeeder::start(VSTPluginInstance* effect)
{
	stop();

	// Field kill switch, doubling as the control arm of the A/B proof
	// behind --vst-panel-feed-test: with the feed disabled the panel
	// behaves exactly as it did before this feature existed.
	if (qEnvironmentVariableIsSet("EAPO_DISABLE_PANEL_FEED"))
		return;

	if (effect == nullptr)
		return;

	const int inputChannelCount = effect->numInputs();
	const int outputChannelCount = effect->numOutputs();
	if (inputChannelCount <= 0 || outputChannelCount <= 0)
		return;

	auto state = std::make_unique<CaptureState>();
	state->effect = effect;
	state->isVst3 = effect->isVST3();
	if (effect->canDoubleReplacing())
		state->width = ProcessWidth::Double64;
	else if (effect->canReplacing())
		state->width = ProcessWidth::Float32;
	else
		state->width = ProcessWidth::Float32Accumulate;

	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
		CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.put()));
	if (FAILED(hr) || !enumerator)
	{
		LogF(L"Panel preview feed: device enumerator failed (0x%08lx)", hr);
		return;
	}

	ComPtr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
	if (FAILED(hr) || !device)
	{
		LogF(L"Panel preview feed: no default render endpoint (0x%08lx)", hr);
		return;
	}

	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
		reinterpret_cast<void**>(state->audioClient.put()));
	if (FAILED(hr) || !state->audioClient)
	{
		LogF(L"Panel preview feed: IAudioClient activation failed (0x%08lx)", hr);
		return;
	}

	hr = state->audioClient->GetMixFormat(state->mixFormat.put());
	if (FAILED(hr) || !state->mixFormat)
	{
		LogF(L"Panel preview feed: GetMixFormat failed (0x%08lx)", hr);
		return;
	}

	// The pump reads the capture buffer as float32 frames. The shared-mode
	// mix format is float32 on every stock Windows configuration, but a
	// format this code did not verify must not be reinterpret_cast away.
	if (!isFloat32Format(state->mixFormat.get()))
	{
		LogF(L"Panel preview feed: mix format is not float32 (tag %u, %u bits), not feeding",
			state->mixFormat->wFormatTag, state->mixFormat->wBitsPerSample);
		return;
	}

	hr = state->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
		captureBufferDuration, 0, state->mixFormat.get(), nullptr);
	if (FAILED(hr))
	{
		LogF(L"Panel preview feed: loopback Initialize failed (0x%08lx)", hr);
		return;
	}

	hr = state->audioClient->GetService(__uuidof(IAudioCaptureClient),
		reinterpret_cast<void**>(state->captureClient.put()));
	if (FAILED(hr) || !state->captureClient)
	{
		LogF(L"Panel preview feed: IAudioCaptureClient failed (0x%08lx)", hr);
		return;
	}

	// VST3 setupProcessing is only legal while the processor is deactivated,
	// which is why start() must precede startEditing() - see the header.
	effect->prepareForProcessing(static_cast<float>(state->mixFormat->nSamplesPerSec), maxBlockFrames);

	if (state->width == ProcessWidth::Double64)
		state->doubleBuffers.allocate(inputChannelCount, outputChannelCount);
	else
		state->floatBuffers.allocate(inputChannelCount, outputChannelCount);

	hr = state->audioClient->Start();
	if (FAILED(hr))
	{
		LogF(L"Panel preview feed: capture Start failed (0x%08lx)", hr);
		return;
	}

	TraceF(L"Panel preview feed: capturing %lu Hz, %u channels into %d/%d plugin channels (%hs)",
		state->mixFormat->nSamplesPerSec, state->mixFormat->nChannels,
		inputChannelCount, outputChannelCount,
		state->width == ProcessWidth::Double64 ? "double64"
		: state->width == ProcessWidth::Float32 ? "float32" : "float32-accumulate");

	// A VST3 instance processes inside the editor session the caller is
	// about to open (startEditing -> beginVST3EditorSession). A VST2 effect
	// has no such session and would be fed while suspended, so the feeder
	// resumes it here and suspends it again in stop().
	if (!state->isVst3)
	{
		effect->startProcessing();
		state->ownsVst2ProcessingState = true;
	}

	capture = std::move(state);
	pumpTimer.start();
}

void PanelPreviewFeeder::stop()
{
	pumpTimer.stop();
	if (capture == nullptr)
		return;
	if (capture->audioClient)
		capture->audioClient->Stop();
	if (capture->ownsVst2ProcessingState)
		capture->effect->stopProcessingSafely();
	capture.reset();
}

// Deliberately on the GUI thread. The VST3 contract demands serialized
// process calls, not a dedicated thread - the spec's own flush pattern has
// the host call process from a UI/timer context whenever no audio engine
// runs, which is exactly this instance's situation. One thread also keeps
// every consumer of the parameter-edit ring and the shared
// inputParameterChanges list serialized for free; a worker thread would
// race them against the GUI flush path. The per-tick frame cap below
// bounds the GUI time a slow plug-in can take.
void PanelPreviewFeeder::pump()
{
	if (capture == nullptr)
		return;
	CaptureState& state = *capture;

	// A VST3 session whose view never attached (startEditing failed) holds
	// no Processing state to feed; the capture is still drained below so it
	// does not pile up.
	const bool processReady = !state.isVst3 || state.effect->vst3EditorSessionActive();

	// Cap the audio processed per tick: after an event-loop stall the
	// capture buffer holds up to captureBufferDuration of backlog, and
	// replaying all of it in one tick would stall the GUI again. The meters
	// show "now" - everything past the cap is drained unprocessed.
	UINT32 remainingFrames = maxBlockFrames;

	UINT32 packetFrames = 0;
	HRESULT hr = state.captureClient->GetNextPacketSize(&packetFrames);
	while (SUCCEEDED(hr) && packetFrames > 0)
	{
		BYTE* data = nullptr;
		UINT32 framesAvailable = 0;
		DWORD flags = 0;
		hr = state.captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr);
		if (FAILED(hr))
			break;

		const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
		const UINT32 captureChannelCount = state.mixFormat->nChannels;
		const float* interleaved = reinterpret_cast<const float*>(data);

		UINT32 offset = 0;
		while (processReady && offset < framesAvailable && remainingFrames > 0)
		{
			const UINT32 chunk = std::min({ framesAvailable - offset, maxBlockFrames, remainingFrames });

			if (state.width == ProcessWidth::Double64)
			{
				state.doubleBuffers.convertInput(interleaved, offset, chunk, captureChannelCount, silent);
			}
			else
			{
				state.floatBuffers.convertInput(interleaved, offset, chunk, captureChannelCount, silent);
				if (state.width == ProcessWidth::Float32Accumulate)
					state.floatBuffers.zeroOutput();
			}

			if (!processGuarded(state.effect, state.width,
				state.floatBuffers.inputPointers.data(), state.floatBuffers.outputPointers.data(),
				state.doubleBuffers.inputPointers.data(), state.doubleBuffers.outputPointers.data(),
				static_cast<int>(chunk)))
			{
				state.captureClient->ReleaseBuffer(framesAvailable);
				stop();
				return;
			}

			offset += chunk;
			remainingFrames -= chunk;
		}

		state.captureClient->ReleaseBuffer(framesAvailable);
		hr = state.captureClient->GetNextPacketSize(&packetFrames);
	}

	// A failing capture client (typically AUDCLNT_E_DEVICE_INVALIDATED after
	// a default-device change) never recovers; stopping here freezes the
	// meters, and reopening the panel restarts the feed on the new device.
	if (FAILED(hr))
		stop();
}
