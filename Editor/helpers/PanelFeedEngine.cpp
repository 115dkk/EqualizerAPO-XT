/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)
*/

#include "PanelFeedEngine.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <cmath>
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
// 500 ms of buffer headroom in 100-ns units, so short event-loop stalls lose
// no capture audio and do not underrun the monitor playback.
constexpr REFERENCE_TIME captureBufferDuration = 5000000;
constexpr REFERENCE_TIME renderBufferDuration = 5000000;
constexpr int pumpIntervalMs = 30;
// How much rendered audio the monitor keeps queued ahead of the device. Far
// larger than a tick, far smaller than the buffer; latency is irrelevant for
// a calibration noise, surviving GUI stalls is not.
constexpr int renderQueueTargetMs = 200;
// The gate opens mid-noise, so the first rendered frames ramp in briefly
// instead of starting with a click.
constexpr int renderFadeMs = 20;
constexpr UINT32 renderBlockFrames = 1024;
// Input below this counts as silence for the monitor gate even without the
// WASAPI silence flag: applications routinely hold an open stream rendering
// unflagged digital zeros (an idle browser tab, a muted voice client), and
// the flag alone would then keep the gate armed never. -80 dBFS is far below
// any audible program material.
constexpr float inputQuietThreshold = 1e-4f;

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

bool bufferIsQuiet(const float* interleaved, UINT32 sampleCount)
{
	for (UINT32 i = 0; i < sampleCount; i++)
	{
		if (std::fabs(interleaved[i]) >= inputQuietThreshold)
			return false;
	}
	return true;
}

int framesToMs(UINT32 frames, UINT32 sampleRate)
{
	return static_cast<int>(frames * 1000ull / sampleRate);
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

	// Peak over the channels that can actually reach the endpoint. A signal
	// on a plugin output channel past the mix width could neither be heard
	// nor rendered, so it must not drive the gate.
	double outputPeak(UINT32 chunk, size_t channelLimit) const
	{
		double peak = 0.0;
		const size_t channels = std::min(channelLimit, outputScratch.size());
		for (size_t channel = 0; channel < channels; channel++)
		{
			for (UINT32 i = 0; i < chunk; i++)
				peak = std::max(peak, static_cast<double>(std::fabs(static_cast<double>(outputScratch[channel][i]))));
		}
		return peak;
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

struct PanelFeedEngine::EngineState
{
	VSTPluginInstance* effect = nullptr;
	Options options;
	bool isVst3 = false;
	ProcessWidth width = ProcessWidth::Float32;
	bool ownsProcessingState = false;
	ComPtr<IMMDevice> device;
	ComPtr<IAudioClient> audioClient;
	ComPtr<IAudioCaptureClient> captureClient;
	CoTaskMem<WAVEFORMATEX> mixFormat;
	PlanarBuffers<float> floatBuffers;
	PlanarBuffers<double> doubleBuffers;
	// min(plugin outputs, endpoint channels): the channels the monitor can
	// make audible, which is also the set the gate listens to.
	UINT32 monitoredChannels = 0;

	PanelMonitorGate gate;
	// The playback half, created lazily the first time the gate opens and
	// kept for later sessions; renderStarted tracks whether the stream is
	// currently running.
	ComPtr<IAudioClient> renderClient;
	ComPtr<IAudioRenderClient> renderService;
	UINT32 renderBufferFrames = 0;
	bool renderStarted = false;
	UINT32 fadeFramesTotal = 0;
	UINT32 fadeFramesRemaining = 0;
	long long renderedFramesTotal = 0;

	bool process(UINT32 chunk)
	{
		return processGuarded(effect, width,
			floatBuffers.inputPointers.data(), floatBuffers.outputPointers.data(),
			doubleBuffers.inputPointers.data(), doubleBuffers.outputPointers.data(),
			static_cast<int>(chunk));
	}

	// Feeds one block of silence through the plugin; shared by the idle half
	// of Listen and the whole of Render.
	bool processSilence(UINT32 chunk)
	{
		if (width == ProcessWidth::Double64)
		{
			doubleBuffers.convertInput(nullptr, 0, chunk, 0, true);
		}
		else
		{
			floatBuffers.convertInput(nullptr, 0, chunk, 0, true);
			if (width == ProcessWidth::Float32Accumulate)
				floatBuffers.zeroOutput();
		}
		return process(chunk);
	}

	double outputPeak(UINT32 chunk) const
	{
		return width == ProcessWidth::Double64
			? doubleBuffers.outputPeak(chunk, monitoredChannels)
			: floatBuffers.outputPeak(chunk, monitoredChannels);
	}

	// Reads one processed output sample as float for the monitor playback.
	float outputSample(UINT32 channel, UINT32 frame) const
	{
		return width == ProcessWidth::Double64
			? static_cast<float>(doubleBuffers.outputScratch[channel][frame])
			: floatBuffers.outputScratch[channel][frame];
	}
};

PanelFeedEngine::PanelFeedEngine() = default;

PanelFeedEngine::~PanelFeedEngine()
{
	stop();
}

int PanelFeedEngine::tickIntervalMs()
{
	return pumpIntervalMs;
}

PanelMonitorGate::State PanelFeedEngine::gateState() const
{
	return state != nullptr ? state->gate.state() : PanelMonitorGate::State::Listen;
}

long long PanelFeedEngine::renderedFrames() const
{
	return state != nullptr ? state->renderedFramesTotal : 0;
}

bool PanelFeedEngine::start(VSTPluginInstance* effect, const Options& options)
{
	stop();

	if (effect == nullptr)
		return false;

	const int inputChannelCount = effect->numInputs();
	const int outputChannelCount = effect->numOutputs();
	if (inputChannelCount <= 0 || outputChannelCount <= 0)
		return false;

	auto s = std::make_unique<EngineState>();
	s->effect = effect;
	s->options = options;
	s->isVst3 = effect->isVST3();
	if (effect->canDoubleReplacing())
		s->width = ProcessWidth::Double64;
	else if (effect->canReplacing())
		s->width = ProcessWidth::Float32;
	else
		s->width = ProcessWidth::Float32Accumulate;

	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
		CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.put()));
	if (FAILED(hr) || !enumerator)
	{
		LogF(L"Panel preview feed: device enumerator failed (0x%08lx)", hr);
		return false;
	}

	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, s->device.put());
	if (FAILED(hr) || !s->device)
	{
		LogF(L"Panel preview feed: no default render endpoint (0x%08lx)", hr);
		return false;
	}

	hr = s->device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
		reinterpret_cast<void**>(s->audioClient.put()));
	if (FAILED(hr) || !s->audioClient)
	{
		LogF(L"Panel preview feed: IAudioClient activation failed (0x%08lx)", hr);
		return false;
	}

	hr = s->audioClient->GetMixFormat(s->mixFormat.put());
	if (FAILED(hr) || !s->mixFormat)
	{
		LogF(L"Panel preview feed: GetMixFormat failed (0x%08lx)", hr);
		return false;
	}

	// The pump reads the capture buffer as float32 frames and writes the
	// monitor playback the same way. The shared-mode mix format is float32
	// on every stock Windows configuration, but a format this code did not
	// verify must not be reinterpret_cast away.
	if (!isFloat32Format(s->mixFormat.get()))
	{
		LogF(L"Panel preview feed: mix format is not float32 (tag %u, %u bits), not feeding",
			s->mixFormat->wFormatTag, s->mixFormat->wBitsPerSample);
		return false;
	}

	hr = s->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
		captureBufferDuration, 0, s->mixFormat.get(), nullptr);
	if (FAILED(hr))
	{
		LogF(L"Panel preview feed: loopback Initialize failed (0x%08lx)", hr);
		return false;
	}

	hr = s->audioClient->GetService(__uuidof(IAudioCaptureClient),
		reinterpret_cast<void**>(s->captureClient.put()));
	if (FAILED(hr) || !s->captureClient)
	{
		LogF(L"Panel preview feed: IAudioCaptureClient failed (0x%08lx)", hr);
		return false;
	}

	// VST3 setupProcessing is only legal while the processor is deactivated,
	// which is why start() must precede the activation - see the header.
	effect->prepareForProcessing(static_cast<float>(s->mixFormat->nSamplesPerSec), maxBlockFrames);

	if (s->width == ProcessWidth::Double64)
		s->doubleBuffers.allocate(inputChannelCount, outputChannelCount);
	else
		s->floatBuffers.allocate(inputChannelCount, outputChannelCount);
	s->monitoredChannels = std::min(static_cast<UINT32>(outputChannelCount),
		static_cast<UINT32>(s->mixFormat->nChannels));

	hr = s->audioClient->Start();
	if (FAILED(hr))
	{
		LogF(L"Panel preview feed: capture Start failed (0x%08lx)", hr);
		return false;
	}

	TraceF(L"Panel preview feed: capturing %lu Hz, %u channels into %d/%d plugin channels (%hs), monitor %hs",
		s->mixFormat->nSamplesPerSec, s->mixFormat->nChannels,
		inputChannelCount, outputChannelCount,
		s->width == ProcessWidth::Double64 ? "double64"
		: s->width == ProcessWidth::Float32 ? "float32" : "float32-accumulate",
		options.monitorEnabled ? "enabled" : "disabled");

	// A VST3 instance inside the Editor processes inside the editor session
	// the caller is about to open (startEditing -> beginVST3EditorSession).
	// A VST2 effect has no such session and would be fed while suspended, so
	// the engine resumes it here and suspends it again in stop(). A headless
	// VST3 harness has no editor session either; the engine then owns the
	// activation the same way.
	if (!s->isVst3 || !options.requireVst3EditorSession)
	{
		effect->startProcessing();
		s->ownsProcessingState = true;
	}

	state = std::move(s);
	return true;
}

void PanelFeedEngine::stop()
{
	if (state == nullptr)
		return;
	if (state->renderStarted && state->renderClient)
		state->renderClient->Stop();
	if (state->audioClient)
		state->audioClient->Stop();
	if (state->ownsProcessingState)
		state->effect->stopProcessingSafely();
	state.reset();
}

// The playback client is created on the same endpoint the capture taps, the
// first time the gate opens, and reused for every later session.
bool PanelFeedEngine::ensureRenderStarted(EngineState& s)
{
	HRESULT hr = S_OK;
	if (!s.renderClient)
	{
		hr = s.device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
			reinterpret_cast<void**>(s.renderClient.put()));
		if (FAILED(hr) || !s.renderClient)
		{
			LogFStatic(L"Panel monitor: render IAudioClient activation failed (0x%08lx)", hr);
			s.renderClient.reset();
			return false;
		}
		hr = s.renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
			renderBufferDuration, 0, s.mixFormat.get(), nullptr);
		if (FAILED(hr))
		{
			LogFStatic(L"Panel monitor: render Initialize failed (0x%08lx)", hr);
			s.renderClient.reset();
			return false;
		}
		hr = s.renderClient->GetService(__uuidof(IAudioRenderClient),
			reinterpret_cast<void**>(s.renderService.put()));
		if (FAILED(hr) || !s.renderService)
		{
			LogFStatic(L"Panel monitor: IAudioRenderClient failed (0x%08lx)", hr);
			s.renderService.reset();
			s.renderClient.reset();
			return false;
		}
		hr = s.renderClient->GetBufferSize(&s.renderBufferFrames);
		if (FAILED(hr) || s.renderBufferFrames == 0)
		{
			LogFStatic(L"Panel monitor: render GetBufferSize failed (0x%08lx)", hr);
			s.renderService.reset();
			s.renderClient.reset();
			return false;
		}
	}

	hr = s.renderClient->Start();
	if (FAILED(hr))
	{
		LogFStatic(L"Panel monitor: render Start failed (0x%08lx)", hr);
		return false;
	}
	s.renderStarted = true;
	s.fadeFramesTotal = s.mixFormat->nSamplesPerSec * renderFadeMs / 1000;
	s.fadeFramesRemaining = s.fadeFramesTotal;
	TraceFStatic(L"Panel monitor: playing self-generated plugin audio");
	return true;
}

void PanelFeedEngine::stopRender(EngineState& s)
{
	if (s.renderStarted && s.renderClient)
	{
		const HRESULT stopResult = s.renderClient->Stop();
		const HRESULT resetResult = s.renderClient->Reset();
		if (FAILED(stopResult) || FAILED(resetResult))
		{
			// A client that would not stop or clear cleanly is not reused;
			// the next gate opening builds a fresh one.
			s.renderService.reset();
			s.renderClient.reset();
		}
	}
	s.renderStarted = false;
}

// An unrecoverable playback failure ends the monitor for this panel session
// but leaves the Listen metering alive; reopening the panel starts over.
void PanelFeedEngine::abandonMonitor(EngineState& s)
{
	stopRender(s);
	s.options.monitorEnabled = false;
	s.gate.reset();
}

// Deliberately single-threaded with the owner. The VST3 contract demands
// serialized process calls, not a dedicated thread - the spec's own flush
// pattern has the host call process from a UI/timer context whenever no
// audio engine runs, which is exactly this instance's situation. One thread
// also keeps every consumer of the parameter-edit ring and the shared
// inputParameterChanges list serialized for free; a worker thread would race
// them against the GUI flush path. The per-tick frame cap bounds the time a
// slow plug-in can take.
bool PanelFeedEngine::tick()
{
	if (state == nullptr)
		return false;
	EngineState& s = *state;

	// A VST3 session whose view never attached (startEditing failed) holds
	// no Processing state to feed; the capture is still drained so it does
	// not pile up.
	const bool processReady = !s.isVst3 || !s.options.requireVst3EditorSession
		|| s.effect->vst3EditorSessionActive();

	if (s.gate.state() == PanelMonitorGate::State::Render)
		return renderTick(s, processReady);
	return listenTick(s, processReady);
}

bool PanelFeedEngine::listenTick(EngineState& s, bool processReady)
{
	// While the plugin cannot process, whatever the gate accumulated earlier
	// no longer describes the present - nobody was measuring.
	if (!processReady)
		s.gate.reset();

	// Cap the audio processed per tick: after an event-loop stall the
	// capture buffer holds up to captureBufferDuration of backlog, and
	// replaying all of it in one tick would stall the GUI again. The meters
	// show "now" - everything past the cap is drained unprocessed.
	UINT32 remainingFrames = maxBlockFrames;
	UINT32 processedFrames = 0;
	bool unprocessedLoudInput = false;

	UINT32 packetFrames = 0;
	HRESULT hr = s.captureClient->GetNextPacketSize(&packetFrames);
	while (SUCCEEDED(hr) && packetFrames > 0)
	{
		BYTE* data = nullptr;
		UINT32 framesAvailable = 0;
		DWORD flags = 0;
		hr = s.captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr);
		if (FAILED(hr))
			break;

		const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
		const UINT32 captureChannelCount = s.mixFormat->nChannels;
		const float* interleaved = reinterpret_cast<const float*>(data);
		const bool packetQuiet = silent
			|| bufferIsQuiet(interleaved, framesAvailable * captureChannelCount);

		UINT32 offset = 0;
		while (processReady && offset < framesAvailable && remainingFrames > 0)
		{
			const UINT32 chunk = std::min({ framesAvailable - offset, maxBlockFrames, remainingFrames });

			if (s.width == ProcessWidth::Double64)
			{
				s.doubleBuffers.convertInput(interleaved, offset, chunk, captureChannelCount, silent);
			}
			else
			{
				s.floatBuffers.convertInput(interleaved, offset, chunk, captureChannelCount, silent);
				if (s.width == ProcessWidth::Float32Accumulate)
					s.floatBuffers.zeroOutput();
			}

			if (!s.process(chunk))
			{
				s.captureClient->ReleaseBuffer(framesAvailable);
				stop();
				return false;
			}
			// The gate advances per processed block, so its open condition
			// really means sustained output over openActiveMs, not one hot
			// sample smeared across a batch.
			s.gate.advance(framesToMs(chunk, s.mixFormat->nSamplesPerSec),
				packetQuiet, s.outputPeak(chunk));

			offset += chunk;
			remainingFrames -= chunk;
			processedFrames += chunk;
		}
		if (!packetQuiet && offset < framesAvailable)
			unprocessedLoudInput = true;

		hr = s.captureClient->ReleaseBuffer(framesAvailable);
		if (FAILED(hr))
			break;
		hr = s.captureClient->GetNextPacketSize(&packetFrames);
	}

	// A failing capture client (typically AUDCLNT_E_DEVICE_INVALIDATED after
	// a default-device change) never recovers; stopping here freezes the
	// meters, and reopening the panel restarts the feed on the new device.
	if (FAILED(hr))
	{
		stop();
		return false;
	}

	// Frames past the processing cap were drained unheard; if they carried
	// audio, the quiet streak they interrupt must not survive them.
	if (unprocessedLoudInput)
		s.gate.advance(0, false, 0.0);

	// Nothing captured this tick - the system is idle and WASAPI delivers no
	// loopback packets. One silence block keeps the plugin's process calls
	// coming, so its meters idle honestly and a signal it generates on its
	// own has somewhere to appear.
	if (processReady && processedFrames == 0)
	{
		const UINT32 chunk = std::min(maxBlockFrames,
			static_cast<UINT32>(s.mixFormat->nSamplesPerSec) * static_cast<UINT32>(pumpIntervalMs) / 1000);
		if (!s.processSilence(chunk))
		{
			stop();
			return false;
		}
		s.gate.advance(framesToMs(chunk, s.mixFormat->nSamplesPerSec),
			true, s.outputPeak(chunk));
	}

	if (s.gate.state() == PanelMonitorGate::State::Render)
	{
		if (!s.options.monitorEnabled || !ensureRenderStarted(s))
			abandonMonitor(s);
	}
	return true;
}

bool PanelFeedEngine::renderTick(EngineState& s, bool processReady)
{
	// The capture keeps running while the gate is open, but what it hears is
	// this monitor's own playback - drained and discarded, never fed back.
	UINT32 packetFrames = 0;
	HRESULT hr = s.captureClient->GetNextPacketSize(&packetFrames);
	while (SUCCEEDED(hr) && packetFrames > 0)
	{
		BYTE* data = nullptr;
		UINT32 framesAvailable = 0;
		DWORD flags = 0;
		hr = s.captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr);
		if (FAILED(hr))
			break;
		hr = s.captureClient->ReleaseBuffer(framesAvailable);
		if (FAILED(hr))
			break;
		hr = s.captureClient->GetNextPacketSize(&packetFrames);
	}
	if (FAILED(hr))
	{
		stop();
		return false;
	}

	// The editor session ended under an open gate; there is nothing left to
	// render. Close down to Listen so a later session starts over cleanly.
	if (!processReady)
	{
		stopRender(s);
		s.gate.reset();
		return true;
	}

	UINT32 padding = 0;
	hr = s.renderClient->GetCurrentPadding(&padding);
	if (FAILED(hr) || padding > s.renderBufferFrames)
	{
		LogF(L"Panel monitor: render padding query failed (0x%08lx, padding %u of %u)",
			hr, padding, s.renderBufferFrames);
		abandonMonitor(s);
		return true;
	}

	const UINT32 sampleRate = s.mixFormat->nSamplesPerSec;
	const UINT32 targetQueueFrames = sampleRate * renderQueueTargetMs / 1000;
	UINT32 toWrite = padding < targetQueueFrames ? targetQueueFrames - padding : 0;
	toWrite = std::min({ toWrite, s.renderBufferFrames - padding, maxBlockFrames });

	const UINT32 mixChannels = s.mixFormat->nChannels;
	UINT32 writtenFrames = 0;
	while (writtenFrames < toWrite)
	{
		const UINT32 chunk = std::min(renderBlockFrames, toWrite - writtenFrames);

		// The plugin generates on its own while the gate is open; its input
		// is silence by definition of the state.
		if (!s.processSilence(chunk))
		{
			stop();
			return false;
		}
		const double peak = s.outputPeak(chunk);

		BYTE* renderData = nullptr;
		hr = s.renderService->GetBuffer(chunk, &renderData);
		if (FAILED(hr))
		{
			LogF(L"Panel monitor: render GetBuffer failed (0x%08lx)", hr);
			abandonMonitor(s);
			return true;
		}
		float* samples = reinterpret_cast<float*>(renderData);
		for (UINT32 i = 0; i < chunk; i++)
		{
			float gain = 1.0f;
			if (s.fadeFramesRemaining > 0)
			{
				gain = 1.0f - static_cast<float>(s.fadeFramesRemaining) / static_cast<float>(s.fadeFramesTotal);
				s.fadeFramesRemaining--;
			}
			// The plugin's channels land on the first endpoint channels (the
			// stereo-onto-stereo case in practice); a wider endpoint keeps
			// its remaining channels silent. A misbehaving plugin must not
			// put non-finite or wild samples on the endpoint.
			for (UINT32 channel = 0; channel < mixChannels; channel++)
			{
				float value = 0.0f;
				if (channel < s.monitoredChannels)
				{
					value = s.outputSample(channel, i) * gain;
					if (!std::isfinite(value))
						value = 0.0f;
					else
						value = std::clamp(value, -2.0f, 2.0f);
				}
				samples[i * mixChannels + channel] = value;
			}
		}
		hr = s.renderService->ReleaseBuffer(chunk, 0);
		if (FAILED(hr))
		{
			LogF(L"Panel monitor: render ReleaseBuffer failed (0x%08lx)", hr);
			abandonMonitor(s);
			return true;
		}
		writtenFrames += chunk;
		s.renderedFramesTotal += chunk;

		if (s.gate.advance(framesToMs(chunk, sampleRate), true, peak)
			== PanelMonitorGate::State::Listen)
		{
			stopRender(s);
			TraceF(L"Panel monitor: plugin went quiet, back to listening");
			break;
		}
	}
	return true;
}
