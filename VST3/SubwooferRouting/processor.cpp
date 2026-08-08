// SPDX-License-Identifier: MIT

#include "processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/Processor.h"
#include "SubwooferRouting/StateCodec.h"
#include "plugin_ids.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace eapoxt::subwooferrouting::vst3
{
namespace
{

bool iidIs(const TUID iid, const FUID& expected)
{
	return FUnknownPrivate::iidEqual(iid, expected);
}

void copyString128(String128 destination, const wchar_t* source)
{
	wcsncpy_s(reinterpret_cast<wchar_t*>(destination), 128, source, _TRUNCATE);
}

double clampNormalized(double value)
{
	if (!std::isfinite(value))
		return 0.0;
	return std::clamp(value, 0.0, 1.0);
}

double normalizedToRange(double normalized, double minimum, double maximum)
{
	return minimum + clampNormalized(normalized) * (maximum - minimum);
}

int parameterSlot(ParamID id)
{
	switch (id)
	{
	case kBypassParamId:
		return 0;
	case kSourceLfeGainParamId:
		return 1;
	case kSourceLfePolarityParamId:
		return 2;
	case kSourceLfeDelayParamId:
		return 3;
	case kOutputTrimParamId:
		return 4;
	case kHeadroomAutoParamId:
		return 5;
	default:
		return -1;
	}
}

// cppcheck-suppress constParameterReference // the caller mutates through the returned pointer
subroute::Path* findSourceLfePath(subroute::SubwooferRoutingState& state)
{
	for (subroute::Path& path : state.paths)
	{
		if (path.kind == subroute::PathKind::SourceLfe)
			return &path;
	}
	return nullptr;
}

bool readExact(IBStream* stream, void* destination, uint32 byteCount)
{
	uint8* output = static_cast<uint8*>(destination);
	uint32 total = 0;
	while (total < byteCount)
	{
		int32 bytesRead = 0;
		const uint32 remaining = byteCount - total;
		const int32 request = static_cast<int32>(
			std::min<uint32>(remaining, static_cast<uint32>(std::numeric_limits<int32>::max())));
		const tresult result = stream->read(output + total, request, &bytesRead);
		if (result != kResultOk || bytesRead <= 0 || bytesRead > request)
			return false;
		total += static_cast<uint32>(bytesRead);
	}
	return true;
}

bool writeExact(IBStream* stream, const void* source, uint32 byteCount)
{
	const uint8* input = static_cast<const uint8*>(source);
	uint32 total = 0;
	while (total < byteCount)
	{
		int32 bytesWritten = 0;
		const uint32 remaining = byteCount - total;
		const int32 request = static_cast<int32>(
			std::min<uint32>(remaining, static_cast<uint32>(std::numeric_limits<int32>::max())));
		const tresult result = stream->write(
			const_cast<uint8*>(input + total),
			request,
			&bytesWritten);
		if (result != kResultOk || bytesWritten <= 0 || bytesWritten > request)
			return false;
		total += static_cast<uint32>(bytesWritten);
	}
	return true;
}

}

struct SubwooferRoutingProcessor::PreparedEngine
{
	subroute::SubwooferRoutingState state;
	std::string canonicalJson;
	subroute::Processor processor;
	std::size_t channelCount = 0;
	std::size_t maximumBlockSize = 0;
	double automaticTrimDb = 0.0;
	std::vector<float> bypassScratch32;
	std::vector<double> bypassScratch64;

	void processBypass(const ProcessData& data) noexcept
	{
		const std::size_t frames = static_cast<std::size_t>(data.numSamples);
		if (data.symbolicSampleSize == kSample64)
		{
			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				std::memcpy(
					bypassScratch64.data() + channel * maximumBlockSize,
					data.inputs[0].channelBuffers64[channel],
					frames * sizeof(double));
			}
			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				std::memcpy(
					data.outputs[0].channelBuffers64[channel],
					bypassScratch64.data() + channel * maximumBlockSize,
					frames * sizeof(double));
			}
		}
		else
		{
			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				std::memcpy(
					bypassScratch32.data() + channel * maximumBlockSize,
					data.inputs[0].channelBuffers32[channel],
					frames * sizeof(float));
			}
			for (std::size_t channel = 0; channel < channelCount; ++channel)
			{
				std::memcpy(
					data.outputs[0].channelBuffers32[channel],
					bypassScratch32.data() + channel * maximumBlockSize,
					frames * sizeof(float));
			}
		}
	}
};

SubwooferRoutingProcessor::SubwooferRoutingProcessor()
	: arrangement_(SpeakerArr::k41Music)
{
	for (std::atomic<double>& value : pendingParameterValues_)
		value.store(0.0, std::memory_order_relaxed);

	setup_.processMode = kRealtime;
	setup_.symbolicSampleSize = kSample64;
	setup_.maxSamplesPerBlock = 1024;
	setup_.sampleRate = 48000.0;
	hasSetup_ = true;

	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	if (!preset.succeeded())
		return;

	state_ = *preset.state;
	const subroute::StateEncodeResult encoded = subroute::encodeStateCanonical(state_);
	if (!encoded.succeeded())
		return;

	canonicalJson_ = *encoded.text;
	try
	{
		publish(buildPrepared(state_, canonicalJson_, arrangement_, setup_));
	}
	catch (...)
	{
	}
}

SubwooferRoutingProcessor::~SubwooferRoutingProcessor()
{
	if (peer_ != nullptr)
		peer_->release();

	PreparedEngine* current = current_.exchange(nullptr, std::memory_order_acq_rel);
	delete current;
	for (PreparedEngine* engine : retired_)
		delete engine;
}

tresult PLUGIN_API SubwooferRoutingProcessor::queryInterface(const TUID iid, void** object)
{
	if (object == nullptr)
		return kInvalidArgument;

	if (iidIs(iid, FUnknown::iid)
		|| iidIs(iid, IPluginBase::iid)
		|| iidIs(iid, IComponent::iid))
	{
		*object = static_cast<IComponent*>(this);
	}
	else if (iidIs(iid, IAudioProcessor::iid))
	{
		*object = static_cast<IAudioProcessor*>(this);
	}
	else if (iidIs(iid, IConnectionPoint::iid))
	{
		*object = static_cast<IConnectionPoint*>(this);
	}
	else
	{
		*object = nullptr;
		return kNoInterface;
	}

	addRef();
	return kResultOk;
}

uint32 PLUGIN_API SubwooferRoutingProcessor::addRef()
{
	return ++refCount_;
}

uint32 PLUGIN_API SubwooferRoutingProcessor::release()
{
	const uint32 remaining = --refCount_;
	if (remaining == 0)
		delete this;
	return remaining;
}

tresult PLUGIN_API SubwooferRoutingProcessor::initialize(FUnknown* context)
{
	if (context == nullptr || initialized_)
		return kResultFalse;
	initialized_ = true;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::terminate()
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	processing_.store(false, std::memory_order_release);
	active_.store(false, std::memory_order_release);
	if (PreparedEngine* engine = current_.load(std::memory_order_acquire))
		engine->processor.reset();
	reapRetired();
	initialized_ = false;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::getControllerClassId(TUID classId)
{
	if (classId == nullptr)
		return kInvalidArgument;
	std::memcpy(classId, kControllerCid, sizeof(Steinberg::TUID));
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setIoMode(IoMode)
{
	return kResultOk;
}

int32 PLUGIN_API SubwooferRoutingProcessor::getBusCount(MediaType type, BusDirection)
{
	return type == kAudio ? 1 : 0;
}

tresult PLUGIN_API SubwooferRoutingProcessor::getBusInfo(
	MediaType type,
	BusDirection direction,
	int32 index,
	BusInfo& info)
{
	if (type != kAudio || index != 0)
		return kInvalidArgument;

	std::lock_guard<std::mutex> lock(stateMutex_);
	std::memset(&info, 0, sizeof(info));
	info.mediaType = kAudio;
	info.direction = direction;
	info.channelCount = SpeakerArr::getChannelCount(arrangement_);
	info.busType = kMain;
	info.flags = BusInfo::kDefaultActive;
	copyString128(info.name, direction == kInput ? L"Subwoofer Routing In" : L"Subwoofer Routing Out");
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::getRoutingInfo(RoutingInfo&, RoutingInfo&)
{
	return kNotImplemented;
}

tresult PLUGIN_API SubwooferRoutingProcessor::activateBus(
	MediaType type,
	BusDirection,
	int32 index,
	TBool)
{
	return type == kAudio && index == 0 ? kResultOk : kInvalidArgument;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setActive(TBool state)
{
	std::lock_guard<std::mutex> lock(stateMutex_);

	if (state == 0)
	{
		processing_.store(false, std::memory_order_release);
		active_.store(false, std::memory_order_release);
		if (PreparedEngine* engine = current_.load(std::memory_order_acquire))
			engine->processor.reset();
		reapRetired();
		return kResultOk;
	}

	applyPendingParametersLocked();
	if (!rebuildCurrentLocked())
		return kResultFalse;

	if (PreparedEngine* engine = current_.load(std::memory_order_acquire))
		engine->processor.reset();

	active_.store(true, std::memory_order_release);
	return kResultOk;
}

bool SubwooferRoutingProcessor::readFramedState(IBStream* stream, std::string& json)
{
	if (stream == nullptr)
		return false;

	uint32 header[2] = {};
	if (!readExact(stream, header, sizeof(header))
		|| header[0] != kStateMagic
		|| header[1] > kMaximumStateBytes)
	{
		return false;
	}

	std::string incoming(header[1], '\0');
	if (header[1] != 0 && !readExact(stream, incoming.data(), header[1]))
		return false;

	json = std::move(incoming);
	return true;
}

tresult SubwooferRoutingProcessor::writeFramedState(IBStream* stream, const std::string& json)
{
	if (stream == nullptr)
		return kInvalidArgument;
	if (json.size() > kMaximumStateBytes)
		return kResultFalse;

	const uint32 header[2] = {
		kStateMagic,
		static_cast<uint32>(json.size())
	};
	if (!writeExact(stream, header, sizeof(header)))
		return kResultFalse;
	if (!json.empty() && !writeExact(stream, json.data(), static_cast<uint32>(json.size())))
		return kResultFalse;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setState(IBStream* stream)
{
	std::string incomingJson;
	if (!readFramedState(stream, incomingJson))
		return kResultFalse;

	const subroute::StateDecodeResult decoded = subroute::decodeState(incomingJson);
	if (!decoded.succeeded())
		return kResultFalse;

	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(*decoded.state);
	if (!encoded.succeeded())
		return kResultFalse;

	std::lock_guard<std::mutex> lock(stateMutex_);
	const int32 stateChannels = static_cast<int32>(decoded.state->layout.channels.size());
	const int32 busChannels = SpeakerArr::getChannelCount(arrangement_);
	if (stateChannels != busChannels)
		return kResultFalse;

	std::unique_ptr<PreparedEngine> replacement;
	try
	{
		replacement = buildPrepared(*decoded.state, *encoded.text, arrangement_, setup_);
	}
	catch (...)
	{
		return kResultFalse;
	}
	if (!replacement)
		return kResultFalse;

	state_ = *decoded.state;
	canonicalJson_ = *encoded.text;
	pendingParameterMask_.store(0, std::memory_order_release);
	publish(std::move(replacement));
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::getState(IBStream* stream)
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	return writeFramedState(stream, canonicalJson_);
}

bool SubwooferRoutingProcessor::isAcceptedArrangement(SpeakerArrangement arrangement)
{
	return arrangement == SpeakerArr::kStereo
		|| arrangement == SpeakerArr::k40Music
		|| arrangement == SpeakerArr::k41Music
		|| arrangement == SpeakerArr::k50
		|| arrangement == SpeakerArr::k51
		|| arrangement == SpeakerArr::k71Music
		|| arrangement == SpeakerArr::k71Cine;
}

std::vector<std::string> SubwooferRoutingProcessor::channelLayoutForArrangement(
	SpeakerArrangement arrangement)
{
	struct Role
	{
		Speaker speaker = 0;
		const char* id = nullptr;
	};

	// k71Cine uses Lc/Rc rather than Sl/Sr. They map to the same canonical
	// side-channel IDs because the Subwoofer Routing state model uses SL/SR.
	const Role roles[] = {
		{kSpeakerL, "L"},
		{kSpeakerR, "R"},
		{kSpeakerC, "C"},
		{kSpeakerLfe, "LFE"},
		{kSpeakerLs, "RL"},
		{kSpeakerRs, "RR"},
		{kSpeakerSl, "SL"},
		{kSpeakerSr, "SR"},
		{kSpeakerLc, "SL"},
		{kSpeakerRc, "SR"}
	};

	const int32 channelCount = SpeakerArr::getChannelCount(arrangement);
	std::vector<std::string> result(static_cast<std::size_t>(channelCount));
	std::vector<bool> assigned(static_cast<std::size_t>(channelCount), false);

	for (const Role& role : roles)
	{
		const int32 index = SpeakerArr::getSpeakerIndex(role.speaker, arrangement);
		if (index < 0 || index >= channelCount)
			continue;
		result[static_cast<std::size_t>(index)] = role.id;
		assigned[static_cast<std::size_t>(index)] = true;
	}

	if (std::find(assigned.begin(), assigned.end(), false) != assigned.end())
		return {};
	return result;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setBusArrangements(
	SpeakerArrangement* inputs,
	int32 numInputs,
	SpeakerArrangement* outputs,
	int32 numOutputs)
{
	if (active_.load(std::memory_order_acquire))
		return kResultFalse;
	if (numInputs != 1 || numOutputs != 1 || inputs == nullptr || outputs == nullptr)
		return kResultFalse;
	if (inputs[0] != outputs[0] || !isAcceptedArrangement(inputs[0]))
		return kResultFalse;

	const SpeakerArrangement proposed = inputs[0];
	const int32 proposedChannels = SpeakerArr::getChannelCount(proposed);

	std::lock_guard<std::mutex> lock(stateMutex_);
	const int32 stateChannels = static_cast<int32>(state_.layout.channels.size());

	// Stereo is accepted unconditionally as the conventional host probe.
	// If it does not match the state, the arrangement is reported but no
	// incompatible engine is retained. A subsequent full-width negotiation
	// prepares the usable engine.
	if (proposed == SpeakerArr::kStereo && proposedChannels != stateChannels)
	{
		arrangement_ = proposed;
		clearPublishedEngineLocked();
		return kResultOk;
	}

	if (proposedChannels != stateChannels)
		return kResultFalse;

	std::unique_ptr<PreparedEngine> replacement;
	try
	{
		replacement = buildPrepared(state_, canonicalJson_, proposed, setup_);
	}
	catch (...)
	{
		return kResultFalse;
	}
	if (!replacement)
		return kResultFalse;

	arrangement_ = proposed;
	publish(std::move(replacement));
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::getBusArrangement(
	BusDirection,
	int32 index,
	SpeakerArrangement& arrangement)
{
	if (index != 0)
		return kInvalidArgument;
	std::lock_guard<std::mutex> lock(stateMutex_);
	arrangement = arrangement_;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64
		? kResultOk
		: kResultFalse;
}

uint32 PLUGIN_API SubwooferRoutingProcessor::getLatencySamples()
{
	return 0;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setupProcessing(ProcessSetup& setup)
{
	if (active_.load(std::memory_order_acquire)
		|| setup.sampleRate <= 0.0
		|| setup.maxSamplesPerBlock <= 0
		|| canProcessSampleSize(setup.symbolicSampleSize) != kResultOk)
	{
		return kResultFalse;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	applyPendingParametersLocked();

	std::unique_ptr<PreparedEngine> replacement;
	try
	{
		replacement = buildPrepared(state_, canonicalJson_, arrangement_, setup);
	}
	catch (...)
	{
		return kResultFalse;
	}
	if (!replacement)
		return kResultFalse;

	setup_ = setup;
	hasSetup_ = true;
	publish(std::move(replacement));
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::setProcessing(TBool state)
{
	if (state != 0 && !active_.load(std::memory_order_acquire))
		return kResultFalse;
	processing_.store(state != 0, std::memory_order_release);
	return kResultOk;
}

void SubwooferRoutingProcessor::consumeParameterChanges(IParameterChanges* changes) noexcept
{
	if (changes == nullptr)
		return;

	const int32 count = changes->getParameterCount();
	for (int32 parameterIndex = 0; parameterIndex < count; ++parameterIndex)
	{
		IParamValueQueue* queue = changes->getParameterData(parameterIndex);
		if (queue == nullptr || queue->getPointCount() <= 0)
			continue;

		const int slot = parameterSlot(queue->getParameterId());
		if (slot < 0)
			continue;

		int32 sampleOffset = 0;
		ParamValue value = 0.0;
		if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != kResultOk)
			continue;

		value = clampNormalized(value);
		if (queue->getParameterId() == kBypassParamId)
		{
			bypass_.store(value >= 0.5, std::memory_order_release);
			continue;
		}

		pendingParameterValues_[slot].store(value, std::memory_order_relaxed);
		pendingParameterMask_.fetch_or(
			static_cast<uint32>(1u << slot),
			std::memory_order_release);
	}
}

tresult PLUGIN_API SubwooferRoutingProcessor::process(ProcessData& data)
{
	consumeParameterChanges(data.inputParameterChanges);

	if (data.numSamples == 0)
		return kResultOk;
	if (!processing_.load(std::memory_order_acquire)
		|| data.symbolicSampleSize != setup_.symbolicSampleSize
		|| data.numInputs != 1
		|| data.numOutputs != 1
		|| data.inputs == nullptr
		|| data.outputs == nullptr)
	{
		return kResultFalse;
	}

	audioReaders_.fetch_add(1, std::memory_order_seq_cst);
	PreparedEngine* engine = current_.load(std::memory_order_seq_cst);
	if (engine == nullptr)
	{
		audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
		return kResultFalse;
	}

	const int32 channelCount = static_cast<int32>(engine->channelCount);
	const bool valid = data.numSamples >= 0
		&& static_cast<std::size_t>(data.numSamples) <= engine->maximumBlockSize
		&& data.inputs[0].numChannels == channelCount
		&& data.outputs[0].numChannels == channelCount;

	if (!valid)
	{
		audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
		return kResultFalse;
	}

	if (data.symbolicSampleSize == kSample64)
	{
		if (data.inputs[0].channelBuffers64 == nullptr
			|| data.outputs[0].channelBuffers64 == nullptr)
		{
			audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
			return kResultFalse;
		}
		for (int32 channel = 0; channel < channelCount; ++channel)
		{
			if (data.inputs[0].channelBuffers64[channel] == nullptr
				|| data.outputs[0].channelBuffers64[channel] == nullptr)
			{
				audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
				return kResultFalse;
			}
		}

		if (bypass_.load(std::memory_order_acquire))
		{
			engine->processBypass(data);
		}
		else
		{
			subroute::AudioBlock block(
				data.inputs[0].channelBuffers64,
				data.outputs[0].channelBuffers64,
				engine->channelCount,
				static_cast<std::size_t>(data.numSamples));
			engine->processor.process(block);
		}
	}
	else
	{
		if (data.inputs[0].channelBuffers32 == nullptr
			|| data.outputs[0].channelBuffers32 == nullptr)
		{
			audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
			return kResultFalse;
		}
		for (int32 channel = 0; channel < channelCount; ++channel)
		{
			if (data.inputs[0].channelBuffers32[channel] == nullptr
				|| data.outputs[0].channelBuffers32[channel] == nullptr)
			{
				audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
				return kResultFalse;
			}
		}

		if (bypass_.load(std::memory_order_acquire))
		{
			engine->processBypass(data);
		}
		else
		{
			subroute::AudioBlock block(
				data.inputs[0].channelBuffers32,
				data.outputs[0].channelBuffers32,
				engine->channelCount,
				static_cast<std::size_t>(data.numSamples));
			engine->processor.process(block);
		}
	}

	// Input silence is deliberately ignored because delay and IIR histories
	// can produce tails. The finite-tail estimate is reflected below.
	data.outputs[0].silenceFlags = 0;
	audioReaders_.fetch_sub(1, std::memory_order_seq_cst);
	return kResultOk;
}

uint32 PLUGIN_API SubwooferRoutingProcessor::getTailSamples()
{
	// The core contains finite delay lines and stable IIR filters. IIR decay is
	// theoretically asymptotic, but two seconds is a conservative practical
	// host tail for the supported subwoofer-routing crossover ranges and avoids
	// falsely claiming either no tail or an infinite synthesizer-style tail.
	const double sampleRate = hasSetup_ && setup_.sampleRate > 0.0
		? setup_.sampleRate
		: 48000.0;
	const double samples = std::ceil(sampleRate * 2.0);
	return samples >= static_cast<double>(std::numeric_limits<uint32>::max())
		? std::numeric_limits<uint32>::max()
		: static_cast<uint32>(samples);
}

tresult PLUGIN_API SubwooferRoutingProcessor::connect(IConnectionPoint* other)
{
	if (other == nullptr)
		return kInvalidArgument;

	std::lock_guard<std::mutex> lock(stateMutex_);
	if (peer_ == other)
		return kResultOk;
	if (peer_ != nullptr)
		peer_->release();
	peer_ = other;
	peer_->addRef();
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::disconnect(IConnectionPoint* other)
{
	std::lock_guard<std::mutex> lock(stateMutex_);
	if (peer_ == nullptr)
		return kResultOk;
	if (other != nullptr && peer_ != other)
		return kResultFalse;
	peer_->release();
	peer_ = nullptr;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingProcessor::notify(IMessage* message)
{
	if (message == nullptr
		|| message->getMessageID() == nullptr
		|| std::strcmp(message->getMessageID(), kParameterMessageId) != 0
		|| message->getAttributes() == nullptr)
	{
		return kInvalidArgument;
	}

	int64 rawId = 0;
	double value = 0.0;
	if (message->getAttributes()->getInt(kMessageParameterId, rawId) != kResultOk
		|| message->getAttributes()->getFloat(kMessageParameterValue, value) != kResultOk)
	{
		return kResultFalse;
	}

	std::lock_guard<std::mutex> lock(stateMutex_);
	return applyParameterLocked(
		static_cast<ParamID>(rawId),
		clampNormalized(value),
		true)
		? kResultOk
		: kResultFalse;
}

std::unique_ptr<SubwooferRoutingProcessor::PreparedEngine>
SubwooferRoutingProcessor::buildPrepared(
	const subroute::SubwooferRoutingState& state,
	const std::string& canonicalJson,
	SpeakerArrangement arrangement,
	const ProcessSetup& setup) const
{
	const std::vector<std::string> channelLayout =
		channelLayoutForArrangement(arrangement);
	if (channelLayout.empty()
		|| channelLayout.size() != state.layout.channels.size()
		|| setup.sampleRate <= 0.0
		|| setup.maxSamplesPerBlock <= 0)
	{
		return nullptr;
	}

	subroute::PrepareSpec specification;
	specification.sampleRate = setup.sampleRate;
	specification.maximumBlockSize =
		static_cast<std::size_t>(setup.maxSamplesPerBlock);
	specification.channelLayout = channelLayout;

	subroute::CompileResult compiled = subroute::compile(state, specification);
	if (!compiled.succeeded())
		return nullptr;

	auto engine = std::make_unique<PreparedEngine>();
	engine->state = state;
	engine->canonicalJson = canonicalJson;
	engine->channelCount = channelLayout.size();
	engine->maximumBlockSize = specification.maximumBlockSize;
	engine->automaticTrimDb = compiled.headroom.has_value()
		? compiled.headroom->appliedTrimDb
		: state.headroom.manualTrimDb;
	engine->bypassScratch32.resize(
		engine->channelCount * engine->maximumBlockSize);
	engine->bypassScratch64.resize(
		engine->channelCount * engine->maximumBlockSize);
	engine->processor.prepare(specification, *compiled.graph);
	return engine;
}

void SubwooferRoutingProcessor::publish(std::unique_ptr<PreparedEngine> replacement)
{
	reapRetired();

	PreparedEngine* raw = replacement.release();
	PreparedEngine* old = current_.exchange(raw, std::memory_order_seq_cst);
	if (old != nullptr)
		retired_.push_back(old);
}

void SubwooferRoutingProcessor::reapRetired()
{
	// Exact generation scheme:
	// 1. The non-RT thread fully constructs and prepares generation N.
	// 2. current_ is atomically exchanged and generation N-1 is retired.
	// 3. At the next non-RT publication, prior retired generations are deleted
	//    only when audioReaders_ is zero. If a block is still in flight, they
	//    remain in retired_ until a later non-RT publication or teardown.
	// The audio thread brackets load/use with audioReaders_, never allocates,
	// never parses JSON, and never deletes a generation.
	if (audioReaders_.load(std::memory_order_seq_cst) != 0)
		return;
	for (PreparedEngine* engine : retired_)
		delete engine;
	retired_.clear();
}

void SubwooferRoutingProcessor::clearPublishedEngineLocked()
{
	publish(nullptr);
}

bool SubwooferRoutingProcessor::applyPendingParametersLocked()
{
	const uint32 mask = pendingParameterMask_.exchange(0, std::memory_order_acq_rel);
	if (mask == 0)
		return true;

	bool changed = false;
	for (int slot = 1; slot < 6; ++slot)
	{
		if ((mask & static_cast<uint32>(1u << slot)) == 0)
			continue;

		ParamID id = kBypassParamId;
		switch (slot)
		{
		case 1:
			id = kSourceLfeGainParamId;
			break;
		case 2:
			id = kSourceLfePolarityParamId;
			break;
		case 3:
			id = kSourceLfeDelayParamId;
			break;
		case 4:
			id = kOutputTrimParamId;
			break;
		case 5:
			id = kHeadroomAutoParamId;
			break;
		default:
			continue;
		}

		changed = applyParameterLocked(
			id,
			pendingParameterValues_[slot].load(std::memory_order_relaxed),
			false)
			|| changed;
	}

	if (!changed)
		return true;

	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(state_);
	if (!encoded.succeeded())
		return false;
	canonicalJson_ = *encoded.text;
	return rebuildCurrentLocked();
}

bool SubwooferRoutingProcessor::applyParameterLocked(
	ParamID id,
	ParamValue normalizedValue,
	bool rebuild)
{
	normalizedValue = clampNormalized(normalizedValue);

	if (id == kBypassParamId)
	{
		bypass_.store(normalizedValue >= 0.5, std::memory_order_release);
		return true;
	}

	subroute::SubwooferRoutingState candidate = state_;
	subroute::Path* sourceLfe = findSourceLfePath(candidate);

	if (id == kSourceLfeGainParamId)
	{
		if (sourceLfe == nullptr)
			return false;
		sourceLfe->preGainDb = normalizedToRange(normalizedValue, -20.0, 20.0);
	}
	else if (id == kSourceLfePolarityParamId)
	{
		if (sourceLfe == nullptr)
			return false;
		bool found = false;
		for (subroute::PathStage& stage : sourceLfe->chain)
		{
			if (subroute::PolarityStage* polarity =
				std::get_if<subroute::PolarityStage>(&stage))
			{
				polarity->inverted = normalizedValue >= 0.5;
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	else if (id == kSourceLfeDelayParamId)
	{
		if (sourceLfe == nullptr)
			return false;
		bool found = false;
		for (subroute::PathStage& stage : sourceLfe->chain)
		{
			if (subroute::DelayStage* delay =
				std::get_if<subroute::DelayStage>(&stage))
			{
				delay->milliseconds =
					normalizedToRange(normalizedValue, 0.0, 100.0);
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	else if (id == kOutputTrimParamId)
	{
		const double requested =
			normalizedToRange(normalizedValue, -40.0, 0.0);
		double automatic = candidate.headroom.manualTrimDb;
		if (const PreparedEngine* engine = current_.load(std::memory_order_acquire))
			automatic = engine->automaticTrimDb;

		candidate.headroom.manualTrimDb = requested;
		if (std::fabs(requested - automatic) > 1.0e-9)
			candidate.headroom.mode = subroute::HeadroomMode::Manual;
	}
	else if (id == kHeadroomAutoParamId)
	{
		candidate.headroom.mode = normalizedValue >= 0.5
			? subroute::HeadroomMode::Auto
			: subroute::HeadroomMode::Manual;
	}
	else
	{
		return false;
	}

	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(candidate);
	if (!encoded.succeeded())
		return false;

	if (!rebuild)
	{
		state_ = std::move(candidate);
		canonicalJson_ = *encoded.text;
		return true;
	}

	std::unique_ptr<PreparedEngine> replacement;
	try
	{
		replacement = buildPrepared(candidate, *encoded.text, arrangement_, setup_);
	}
	catch (...)
	{
		return false;
	}
	if (!replacement)
		return false;

	state_ = std::move(candidate);
	canonicalJson_ = *encoded.text;
	publish(std::move(replacement));
	return true;
}

bool SubwooferRoutingProcessor::rebuildCurrentLocked()
{
	std::unique_ptr<PreparedEngine> replacement;
	try
	{
		replacement = buildPrepared(state_, canonicalJson_, arrangement_, setup_);
	}
	catch (...)
	{
		return false;
	}
	if (!replacement)
		return false;
	publish(std::move(replacement));
	return true;
}

FUnknown* createSubwooferRoutingProcessor()
{
	return static_cast<IComponent*>(new SubwooferRoutingProcessor());
}

}
