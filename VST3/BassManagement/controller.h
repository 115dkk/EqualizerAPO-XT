// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <mutex>

#include "BassManagement/State.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"

namespace eapoxt::bassmanagement::vst3
{

class BassManagementController final
	: public Steinberg::Vst::IEditController
	, public Steinberg::Vst::IConnectionPoint
{
public:
	BassManagementController();

	Steinberg::tresult PLUGIN_API queryInterface(
		const Steinberg::TUID iid,
		void** object) override;
	Steinberg::uint32 PLUGIN_API addRef() override;
	Steinberg::uint32 PLUGIN_API release() override;

	Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
	Steinberg::tresult PLUGIN_API terminate() override;
	Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* stream) override;
	Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* stream) override;
	Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* stream) override;
	Steinberg::int32 PLUGIN_API getParameterCount() override;
	Steinberg::tresult PLUGIN_API getParameterInfo(
		Steinberg::int32 index,
		Steinberg::Vst::ParameterInfo& info) override;
	Steinberg::tresult PLUGIN_API getParamStringByValue(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue value,
		Steinberg::Vst::String128 string) override;
	Steinberg::tresult PLUGIN_API getParamValueByString(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::TChar* string,
		Steinberg::Vst::ParamValue& value) override;
	Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue value) override;
	Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue value) override;
	Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(
		Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API setParamNormalized(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue value) override;
	Steinberg::tresult PLUGIN_API setComponentHandler(
		Steinberg::Vst::IComponentHandler* handler) override;
	Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

	Steinberg::tresult PLUGIN_API connect(
		Steinberg::Vst::IConnectionPoint* other) override;
	Steinberg::tresult PLUGIN_API disconnect(
		Steinberg::Vst::IConnectionPoint* other) override;
	Steinberg::tresult PLUGIN_API notify(
		Steinberg::Vst::IMessage* message) override;

private:
	~BassManagementController();

	static bool readFramedState(Steinberg::IBStream* stream, std::string& json);
	void updateValuesFromState(const bassmgmt::BassManagementState& state);
	bool sendParameter(
		Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue value);
	int parameterIndex(Steinberg::Vst::ParamID id) const;

	std::atomic<Steinberg::uint32> refCount_{1};
	bool initialized_ = false;
	mutable std::mutex mutex_;
	Steinberg::Vst::IHostApplication* host_ = nullptr;
	Steinberg::Vst::IComponentHandler* handler_ = nullptr;
	Steinberg::Vst::IConnectionPoint* peer_ = nullptr;
	bassmgmt::BassManagementState state_;
	Steinberg::Vst::ParamValue values_[6] = {};
	Steinberg::Vst::ParamValue defaults_[6] = {};
	double automaticTrimDb_ = 0.0;
};

Steinberg::FUnknown* createBassManagementController();

}
