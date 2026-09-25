// SPDX-License-Identifier: MIT

#include "controller.h"

#include <algorithm>
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

bool iidIs(const TUID iid, const FUID& expected)
{
	return FUnknownPrivate::iidEqual(iid, expected);
}

void copyString128(String128 destination, const wchar_t* source)
{
	wcsncpy_s(reinterpret_cast<wchar_t*>(destination), 128, source, _TRUNCATE);
}

bool readExact(IBStream* stream, void* destination, std::size_t byteCount)
{
	uint8* output = static_cast<uint8*>(destination);
	std::size_t total = 0;
	while (total < byteCount)
	{
		int32 bytesRead = 0;
		const std::size_t remaining = byteCount - total;
		const int32 request = static_cast<int32>(
			std::min<std::size_t>(
				remaining,
				static_cast<std::size_t>(std::numeric_limits<int32>::max())));
		if (stream->read(output + total, request, &bytesRead) != kResultOk
			|| bytesRead <= 0
			|| bytesRead > request)
		{
			return false;
		}
		total += static_cast<std::size_t>(bytesRead);
	}
	return true;
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

tresult PLUGIN_API SubwooferRoutingController::setComponentState(IBStream* stream)
{
	if (stream == nullptr)
		return kResultFalse;

	std::string json;
	if (!readStateFrame(
		[stream](void* destination, std::size_t byteCount)
		{
			return readExact(stream, destination, byteCount);
		},
		json))
	{
		return kResultFalse;
	}

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
	return static_cast<int32>(kParameterCount);
}

tresult PLUGIN_API SubwooferRoutingController::getParameterInfo(int32 index, ParameterInfo& info)
{
	if (index < 0)
		return kInvalidArgument;

	const ParameterDescriptor* parameter =
		parameterBySlot(static_cast<std::size_t>(index));
	if (parameter == nullptr)
		return kInvalidArgument;

	std::memset(&info, 0, sizeof(info));
	info.id = parameter->id;
	info.unitId = kRootUnitId;
	info.defaultNormalizedValue = defaults_[parameter->slot];
	info.flags = ParameterInfo::kCanAutomate;
	copyString128(info.title, parameter->title);
	copyString128(info.shortTitle, parameter->shortTitle);
	copyString128(info.units, parameter->unit);

	if (parameter->kind == ParameterKind::Bypass
		|| parameter->kind == ParameterKind::Polarity
		|| parameter->kind == ParameterKind::HeadroomAuto)
	{
		info.stepCount = 1;
	}
	if (parameter->kind == ParameterKind::Bypass)
		info.flags |= ParameterInfo::kIsBypass;

	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::getParamStringByValue(
	ParamID id,
	ParamValue value,
	String128 string)
{
	const ParameterDescriptor* parameter = parameterById(id);
	if (parameter == nullptr)
		return kInvalidArgument;

	copyString128(string, displayParameterValue(*parameter, value).c_str());
	return kResultOk;
}

tresult PLUGIN_API SubwooferRoutingController::getParamValueByString(
	ParamID id,
	TChar* string,
	ParamValue& value)
{
	const ParameterDescriptor* parameter = parameterById(id);
	if (parameter == nullptr || string == nullptr)
		return kInvalidArgument;

	const wchar_t* text = reinterpret_cast<const wchar_t*>(string);
	if (parameter->kind == ParameterKind::Bypass)
	{
		value = (_wcsicmp(text, L"on") == 0 || _wcsicmp(text, L"true") == 0)
			? 1.0
			: 0.0;
		return kResultOk;
	}
	if (parameter->kind == ParameterKind::Polarity)
	{
		value = (_wcsicmp(text, L"inverted") == 0 || _wcsicmp(text, L"invert") == 0)
			? 1.0
			: 0.0;
		return kResultOk;
	}
	if (parameter->kind == ParameterKind::HeadroomAuto)
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
	const ParameterDescriptor* parameter = parameterById(id);
	return parameter != nullptr
		? normalizedParameterToPlain(*parameter, value)
		: 0.0;
}

ParamValue PLUGIN_API SubwooferRoutingController::plainParamToNormalized(
	ParamID id,
	ParamValue value)
{
	const ParameterDescriptor* parameter = parameterById(id);
	return parameter != nullptr
		? plainParameterToNormalized(*parameter, value)
		: 0.0;
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

	value = clampNormalizedParameter(value);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const ParameterDescriptor* parameter = parameterById(id);
		if (parameter == nullptr)
			return kInvalidArgument;

		values_[parameter->slot] = value;
		if (parameter->kind == ParameterKind::OutputTrim)
		{
			subroute::SubwooferRoutingState candidate = state_;
			bool bypass = values_[0] >= 0.5;
			if (writeNormalizedParameter(
				*parameter,
				candidate,
				value,
				automaticTrimDb_,
				bypass))
			{
				const ParameterDescriptor* headroom =
					parameterById(kHeadroomAutoParamId);
				if (headroom != nullptr)
				{
					readNormalizedParameter(
						*headroom,
						candidate,
						automaticTrimDb_,
						bypass,
						values_[headroom->slot]);
				}
			}
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

tresult PLUGIN_API SubwooferRoutingController::notify(IMessage* message)
{
	if (message == nullptr
		|| message->getMessageID() == nullptr
		|| std::strcmp(message->getMessageID(), kSampleRateMessageId) != 0
		|| message->getAttributes() == nullptr)
	{
		return kResultOk;
	}

	double sampleRate = 0.0;
	if (message->getAttributes()->getFloat(kMessageSampleRate, sampleRate) != kResultOk
		|| !std::isfinite(sampleRate)
		|| sampleRate <= 0.0)
	{
		return kResultFalse;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	previewSampleRate_ = sampleRate;
	updateTrimFromState(state_);
	return kResultOk;
}

void SubwooferRoutingController::updateValuesFromState(
	const subroute::SubwooferRoutingState& state)
{
	updateTrimFromState(state);
	for (std::size_t slot = 0; slot < kParameterCount; ++slot)
	{
		const ParameterDescriptor* parameter = parameterBySlot(slot);
		if (parameter != nullptr)
		{
			readNormalizedParameter(
				*parameter,
				state,
				automaticTrimDb_,
				false,
				values_[slot]);
		}
	}
}

// The headroom preview compiles at the processor's rate once it has reported
// one (audit #348 C6); it used to be a hard-coded 48 kHz.
void SubwooferRoutingController::updateTrimFromState(
	const subroute::SubwooferRoutingState& state)
{
	const subroute::CompileResult compiled = subroute::compile(
		state, subroute::previewSpecFor(state, previewSampleRate_));
	automaticTrimDb_ = compiled.succeeded() && compiled.headroom.has_value()
		? compiled.headroom->appliedTrimDb
		: state.headroom.manualTrimDb;

	const ParameterDescriptor* trim = parameterById(kOutputTrimParamId);
	if (trim != nullptr)
	{
		readNormalizedParameter(
			*trim,
			state,
			automaticTrimDb_,
			false,
			values_[trim->slot]);
	}
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

int SubwooferRoutingController::parameterIndex(ParamID id)
{
	const ParameterDescriptor* parameter = parameterById(id);
	return parameter != nullptr ? static_cast<int>(parameter->slot) : -1;
}

FUnknown* createSubwooferRoutingController()
{
	return static_cast<IEditController*>(new SubwooferRoutingController());
}

}
