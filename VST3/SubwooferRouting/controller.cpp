// SPDX-License-Identifier: MIT

#include "controller.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "plugin_ids.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstunits.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace eapoxt::subwooferrouting::vst3
{
namespace
{

const ParamID parameterIds[] = {
	kBypassParamId,
	kSourceLfeGainParamId,
	kSourceLfePolarityParamId,
	kSourceLfeDelayParamId,
	kOutputTrimParamId,
	kHeadroomAutoParamId
};

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

double toNormalized(double value, double minimum, double maximum)
{
	return clampNormalized((value - minimum) / (maximum - minimum));
}

bool readExact(IBStream* stream, void* destination, uint32 byteCount)
{
	uint8* output = static_cast<uint8*>(destination);
	uint32 total = 0;
	while (total < byteCount)
	{
		int32 bytesRead = 0;
		const int32 request = static_cast<int32>(byteCount - total);
		if (stream->read(output + total, request, &bytesRead) != kResultOk
			|| bytesRead <= 0
			|| bytesRead > request)
		{
			return false;
		}
		total += static_cast<uint32>(bytesRead);
	}
	return true;
}

const subroute::Path* findSourceLfePath(const subroute::SubwooferRoutingState& state)
{
	for (const subroute::Path& path : state.paths)
	{
		if (path.kind == subroute::PathKind::SourceLfe)
			return &path;
	}
	return nullptr;
}

}

SubwooferRoutingController::SubwooferRoutingController()
{
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	if (preset.succeeded())
	{
		state_ = *preset.state;
		updateValuesFromState(state_);
	}
	std::copy(std::begin(values_), std::end(values_), std::begin(defaults_));
}

SubwooferRoutingController::~SubwooferRoutingController()
{
	if (peer_ != nullptr)
		peer_->release();
	if (handler_ != nullptr)
		handler_->release();
	if (host_ != nullptr)
		host_->release();
}

tresult PLUGIN_API SubwooferRoutingController::queryInterface(const TUID iid, void** object)
{
	if (object == nullptr)
		return kInvalidArgument;

	if (iidIs(iid, FUnknown::iid)
		|| iidIs(iid, IPluginBase::iid)
		|| iidIs(iid, IEditController::iid))
	{
		*object = static_cast<IEditController*>(this);
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

uint32 PLUGIN_API SubwooferRoutingController::addRef()
{
	return ++refCount_;
}

uint32 PLUGIN_API SubwooferRoutingController::release()
{
	const uint32 remaining = --refCount_;
	if (remaining == 0)
		delete this;
	return remaining;
}

tresult PLUGIN_API SubwooferRoutingController::initialize(FUnknown* context)
{
	if (context == nullptr || initialized_)
		return kResultFalse;

	IHostApplication* host = nullptr;
	if (context->queryInterface(
		IHostApplication::iid,
		reinterpret_cast<void**>(&host)) != kResultOk
		|| host == nullptr)
	{
		return kResultFalse;
	}

	host_ = host;
	initialized_ = true;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::terminate()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (peer_ != nullptr)
	{
		peer_->release();
		peer_ = nullptr;
	}
	if (handler_ != nullptr)
	{
		handler_->release();
		handler_ = nullptr;
	}
	if (host_ != nullptr)
	{
		host_->release();
		host_ = nullptr;
	}
	initialized_ = false;
	return kResultOk;
}

bool SubwooferRoutingController::readFramedState(IBStream* stream, std::string& json)
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

tresult PLUGIN_API SubwooferRoutingController::setComponentState(IBStream* stream)
{
	std::string json;
	if (!readFramedState(stream, json))
		return kResultFalse;

	const subroute::StateDecodeResult decoded = subroute::decodeState(json);
	if (!decoded.succeeded())
		return kResultFalse;

	std::lock_guard<std::mutex> lock(mutex_);
	state_ = *decoded.state;
	updateValuesFromState(state_);
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::setState(IBStream*)
{
	return kNotImplemented;
}

tresult PLUGIN_API SubwooferRoutingController::getState(IBStream*)
{
	return kNotImplemented;
}

int32 PLUGIN_API SubwooferRoutingController::getParameterCount()
{
	return 6;
}

tresult PLUGIN_API SubwooferRoutingController::getParameterInfo(int32 index, ParameterInfo& info)
{
	if (index < 0 || index >= getParameterCount())
		return kInvalidArgument;

	std::memset(&info, 0, sizeof(info));
	info.id = parameterIds[index];
	info.unitId = kRootUnitId;
	info.defaultNormalizedValue = defaults_[index];
	info.flags = ParameterInfo::kCanAutomate;

	switch (info.id)
	{
	case kBypassParamId:
		copyString128(info.title, L"Bypass");
		copyString128(info.shortTitle, L"Bypass");
		info.stepCount = 1;
		info.flags |= ParameterInfo::kIsBypass;
		break;
	case kSourceLfeGainParamId:
		copyString128(info.title, L"Source LFE Gain");
		copyString128(info.shortTitle, L"LFE Gain");
		copyString128(info.units, L"dB");
		break;
	case kSourceLfePolarityParamId:
		copyString128(info.title, L"Source LFE Polarity");
		copyString128(info.shortTitle, L"LFE Pol");
		info.stepCount = 1;
		break;
	case kSourceLfeDelayParamId:
		copyString128(info.title, L"Source LFE Delay");
		copyString128(info.shortTitle, L"LFE Delay");
		copyString128(info.units, L"ms");
		break;
	case kOutputTrimParamId:
		copyString128(info.title, L"Global Output Trim");
		copyString128(info.shortTitle, L"Trim");
		copyString128(info.units, L"dB");
		break;
	case kHeadroomAutoParamId:
		copyString128(info.title, L"Headroom Auto");
		copyString128(info.shortTitle, L"Headroom");
		info.stepCount = 1;
		break;
	default:
		return kInvalidArgument;
	}

	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::getParamStringByValue(
	ParamID id,
	ParamValue value,
	String128 string)
{
	if (parameterIndex(id) < 0)
		return kInvalidArgument;

	const double plain = normalizedParamToPlain(id, value);
	switch (id)
	{
	case kBypassParamId:
		copyString128(string, plain >= 0.5 ? L"On" : L"Off");
		break;
	case kSourceLfePolarityParamId:
		copyString128(string, plain >= 0.5 ? L"Inverted" : L"Normal");
		break;
	case kHeadroomAutoParamId:
		copyString128(string, plain >= 0.5 ? L"Auto" : L"Manual");
		break;
	case kSourceLfeGainParamId:
	case kOutputTrimParamId:
		swprintf_s(reinterpret_cast<wchar_t*>(string), 128, L"%.2f dB", plain);
		break;
	case kSourceLfeDelayParamId:
		swprintf_s(reinterpret_cast<wchar_t*>(string), 128, L"%.2f ms", plain);
		break;
	default:
		return kInvalidArgument;
	}
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::getParamValueByString(
	ParamID id,
	TChar* string,
	ParamValue& value)
{
	if (parameterIndex(id) < 0 || string == nullptr)
		return kInvalidArgument;

	const wchar_t* text = reinterpret_cast<const wchar_t*>(string);
	if (id == kBypassParamId)
	{
		value = (_wcsicmp(text, L"on") == 0 || _wcsicmp(text, L"true") == 0)
			? 1.0
			: 0.0;
		return kResultOk;
	}
	if (id == kSourceLfePolarityParamId)
	{
		value = (_wcsicmp(text, L"inverted") == 0 || _wcsicmp(text, L"invert") == 0)
			? 1.0
			: 0.0;
		return kResultOk;
	}
	if (id == kHeadroomAutoParamId)
	{
		value = _wcsicmp(text, L"auto") == 0 ? 1.0 : 0.0;
		return kResultOk;
	}

	wchar_t* end = nullptr;
	const double plain = wcstod(text, &end);
	if (end == text || !std::isfinite(plain))
		return kResultFalse;
	value = plainParamToNormalized(id, plain);
	return kResultOk;
}

ParamValue PLUGIN_API SubwooferRoutingController::normalizedParamToPlain(
	ParamID id,
	ParamValue value)
{
	value = clampNormalized(value);
	switch (id)
	{
	case kSourceLfeGainParamId:
		return -20.0 + value * 40.0;
	case kSourceLfeDelayParamId:
		return value * 100.0;
	case kOutputTrimParamId:
		return -40.0 + value * 40.0;
	case kBypassParamId:
	case kSourceLfePolarityParamId:
	case kHeadroomAutoParamId:
		return value >= 0.5 ? 1.0 : 0.0;
	default:
		return 0.0;
	}
}

ParamValue PLUGIN_API SubwooferRoutingController::plainParamToNormalized(
	ParamID id,
	ParamValue value)
{
	switch (id)
	{
	case kSourceLfeGainParamId:
		return toNormalized(value, -20.0, 20.0);
	case kSourceLfeDelayParamId:
		return toNormalized(value, 0.0, 100.0);
	case kOutputTrimParamId:
		return toNormalized(value, -40.0, 0.0);
	case kBypassParamId:
	case kSourceLfePolarityParamId:
	case kHeadroomAutoParamId:
		return value >= 0.5 ? 1.0 : 0.0;
	default:
		return 0.0;
	}
}

ParamValue PLUGIN_API SubwooferRoutingController::getParamNormalized(ParamID id)
{
	const int index = parameterIndex(id);
	if (index < 0)
		return 0.0;
	std::lock_guard<std::mutex> lock(mutex_);
	return values_[index];
}

tresult PLUGIN_API SubwooferRoutingController::setParamNormalized(
	ParamID id,
	ParamValue value)
{
	const int index = parameterIndex(id);
	if (index < 0)
		return kInvalidArgument;

	value = clampNormalized(value);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		values_[index] = value;

		if (id == kOutputTrimParamId)
		{
			const double plain = normalizedParamToPlain(id, value);
			if (std::fabs(plain - automaticTrimDb_) > 1.0e-9)
				values_[parameterIndex(kHeadroomAutoParamId)] = 0.0;
		}
	}

	return sendParameter(id, value) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API SubwooferRoutingController::setComponentHandler(
	IComponentHandler* handler)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (handler_ != nullptr)
		handler_->release();
	handler_ = handler;
	if (handler_ != nullptr)
		handler_->addRef();
	return kResultOk;
}

IPlugView* PLUGIN_API SubwooferRoutingController::createView(FIDString)
{
	return nullptr;
}

tresult PLUGIN_API SubwooferRoutingController::connect(IConnectionPoint* other)
{
	if (other == nullptr)
		return kInvalidArgument;

	std::lock_guard<std::mutex> lock(mutex_);
	if (peer_ == other)
		return kResultOk;
	if (peer_ != nullptr)
		peer_->release();
	peer_ = other;
	peer_->addRef();
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::disconnect(IConnectionPoint* other)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (peer_ == nullptr)
		return kResultOk;
	if (other != nullptr && peer_ != other)
		return kResultFalse;
	peer_->release();
	peer_ = nullptr;
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::notify(IMessage*)
{
	return kResultOk;
}

void SubwooferRoutingController::updateValuesFromState(
	const subroute::SubwooferRoutingState& state)
{
	values_[0] = 0.0;

	const subroute::Path* sourceLfe = findSourceLfePath(state);
	if (sourceLfe != nullptr)
	{
		values_[1] = toNormalized(sourceLfe->preGainDb, -20.0, 20.0);
		values_[2] = 0.0;
		values_[3] = 0.0;

		for (const subroute::PathStage& stage : sourceLfe->chain)
		{
			if (const subroute::PolarityStage* polarity =
				std::get_if<subroute::PolarityStage>(&stage))
			{
				values_[2] = polarity->inverted ? 1.0 : 0.0;
			}
			else if (const subroute::DelayStage* delay =
				std::get_if<subroute::DelayStage>(&stage))
			{
				values_[3] = toNormalized(delay->milliseconds, 0.0, 100.0);
			}
		}
	}

	subroute::PrepareSpec specification;
	specification.sampleRate = 48000.0;
	specification.maximumBlockSize = 1024;
	for (const subroute::PhysicalChannel& channel : state.layout.channels)
		specification.channelLayout.push_back(channel.id);

	const subroute::CompileResult compiled = subroute::compile(state, specification);
	automaticTrimDb_ = compiled.succeeded() && compiled.headroom.has_value()
		? compiled.headroom->appliedTrimDb
		: state.headroom.manualTrimDb;

	const double displayedTrim = state.headroom.mode == subroute::HeadroomMode::Auto
		? automaticTrimDb_
		: state.headroom.manualTrimDb;
	values_[4] = toNormalized(displayedTrim, -40.0, 0.0);
	values_[5] = state.headroom.mode == subroute::HeadroomMode::Auto ? 1.0 : 0.0;
}

bool SubwooferRoutingController::sendParameter(ParamID id, ParamValue value)
{
	IHostApplication* host = nullptr;
	IConnectionPoint* peer = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		host = host_;
		peer = peer_;
		if (host != nullptr)
			host->addRef();
		if (peer != nullptr)
			peer->addRef();
	}

	if (host == nullptr || peer == nullptr)
	{
		if (host != nullptr)
			host->release();
		if (peer != nullptr)
			peer->release();
		return false;
	}

	TUID messageIid;
	IMessage::iid.toTUID(messageIid);
	IMessage* message = nullptr;
	const tresult created = host->createInstance(
		messageIid,
		messageIid,
		reinterpret_cast<void**>(&message));
	host->release();

	if (created != kResultOk
		|| message == nullptr
		|| message->getAttributes() == nullptr)
	{
		if (message != nullptr)
			message->release();
		peer->release();
		return false;
	}

	message->setMessageID(kParameterMessageId);
	message->getAttributes()->setInt(kMessageParameterId, static_cast<int64>(id));
	message->getAttributes()->setFloat(kMessageParameterValue, value);
	const tresult result = peer->notify(message);
	message->release();
	peer->release();
	return result == kResultOk;
}

int SubwooferRoutingController::parameterIndex(ParamID id) const
{
	for (int index = 0; index < 6; ++index)
	{
		if (parameterIds[index] == id)
			return index;
	}
	return -1;
}

FUnknown* createSubwooferRoutingController()
{
	return static_cast<IEditController*>(new SubwooferRoutingController());
}

}
