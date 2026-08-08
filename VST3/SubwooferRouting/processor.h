// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "SubwooferRouting/State.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmessage.h"

namespace eapoxt::subwooferrouting::vst3
{

class SubwooferRoutingProcessor final
	: public Steinberg::Vst::IComponent
	, public Steinberg::Vst::IAudioProcessor
	, public Steinberg::Vst::IConnectionPoint
{
public:
	SubwooferRoutingProcessor();

	Steinberg::tresult PLUGIN_API queryInterface(
		const Steinberg::TUID iid,
		void** object) override;
	Steinberg::uint32 PLUGIN_API addRef() override;
	Steinberg::uint32 PLUGIN_API release() override;

	Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
	Steinberg::tresult PLUGIN_API terminate() override;
	Steinberg::tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;
	Steinberg::tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) override;
	Steinberg::int32 PLUGIN_API getBusCount(
		Steinberg::Vst::MediaType type,
		Steinberg::Vst::BusDirection direction) override;
	Steinberg::tresult PLUGIN_API getBusInfo(
		Steinberg::Vst::MediaType type,
		Steinberg::Vst::BusDirection direction,
		Steinberg::int32 index,
		Steinberg::Vst::BusInfo& info) override;
	Steinberg::tresult PLUGIN_API getRoutingInfo(
		Steinberg::Vst::RoutingInfo& input,
		Steinberg::Vst::RoutingInfo& output) override;
	Steinberg::tresult PLUGIN_API activateBus(
		Steinberg::Vst::MediaType type,
		Steinberg::Vst::BusDirection direction,
		Steinberg::int32 index,
		Steinberg::TBool state) override;
	Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
	Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* stream) override;
	Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* stream) override;

	Steinberg::tresult PLUGIN_API setBusArrangements(
		Steinberg::Vst::SpeakerArrangement* inputs,
		Steinberg::int32 numInputs,
		Steinberg::Vst::SpeakerArrangement* outputs,
		Steinberg::int32 numOutputs) override;
	Steinberg::tresult PLUGIN_API getBusArrangement(
		Steinberg::Vst::BusDirection direction,
		Steinberg::int32 index,
		Steinberg::Vst::SpeakerArrangement& arrangement) override;
	Steinberg::tresult PLUGIN_API canProcessSampleSize(
		Steinberg::int32 symbolicSampleSize) override;
	Steinberg::uint32 PLUGIN_API getLatencySamples() override;
	Steinberg::tresult PLUGIN_API setupProcessing(
		Steinberg::Vst::ProcessSetup& setup) override;
	Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;
	Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
	Steinberg::uint32 PLUGIN_API getTailSamples() override;

	Steinberg::tresult PLUGIN_API connect(
		Steinberg::Vst::IConnectionPoint* other) override;
	Steinberg::tresult PLUGIN_API disconnect(
		Steinberg::Vst::IConnectionPoint* other) override;
	Steinberg::tresult PLUGIN_API notify(
		Steinberg::Vst::IMessage* message) override;

private:
	struct PreparedEngine;

	~SubwooferRoutingProcessor();

	static bool isAcceptedArrangement(Steinberg::Vst::SpeakerArrangement arrangement);
	static std::vector<std::string> channelLayoutForArrangement(
		Steinberg::Vst::SpeakerArrangement arrangement);
	static bool readFramedState(Steinberg::IBStream* stream, std::string& json);
	static Steinberg::tresult writeFramedState(
		Steinberg::IBStream* stream,
		const std::string& json);

	std::unique_ptr<PreparedEngine> buildPrepared(
		const subroute::SubwooferRoutingState& state,
		const std::string& canonicalJson,
		Steinberg::Vst::SpeakerArrangement arrangement,
		const Steinberg::Vst::ProcessSetup& setup) const;
	void publish(std::unique_ptr<PreparedEngine> engine);
	void reapRetired();
	void consumeParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
	bool applyPendingParametersLocked();
	bool applyParameterLocked(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue normalizedValue,
		bool rebuild);
	bool rebuildCurrentLocked();
	void clearPublishedEngineLocked();

	std::atomic<Steinberg::uint32> refCount_{1};
	std::atomic<bool> active_{false};
	std::atomic<bool> processing_{false};
	bool initialized_ = false;
	bool hasSetup_ = false;

	mutable std::mutex stateMutex_;
	subroute::SubwooferRoutingState state_;
	std::string canonicalJson_;
	Steinberg::Vst::SpeakerArrangement arrangement_;
	Steinberg::Vst::ProcessSetup setup_{};

	std::atomic<PreparedEngine*> current_{nullptr};
	std::atomic<Steinberg::uint32> audioReaders_{0};
	std::vector<PreparedEngine*> retired_;

	std::atomic<bool> bypass_{false};
	std::atomic<Steinberg::uint32> pendingParameterMask_{0};
	std::atomic<double> pendingParameterValues_[6];

	Steinberg::Vst::IConnectionPoint* peer_ = nullptr;
};

Steinberg::FUnknown* createSubwooferRoutingProcessor();

}
