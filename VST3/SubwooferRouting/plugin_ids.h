// SPDX-License-Identifier: MIT

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace eapoxt::subwooferrouting::vst3
{

// Stable class IDs registered for EAPO XT Subwoofer Routing.
// They must never be reused by another VST3 class. INLINE_UID keeps these
// header-only: the FUID value constructor lives in the SDK's funknown.cpp,
// which this repository does not compile (pluginterfaces headers only).
inline constexpr Steinberg::TUID kComponentCid =
	INLINE_UID(0x8E75B0A1, 0x29DE4B37, 0xA6C2F911, 0x5D2048E3);

inline constexpr Steinberg::TUID kControllerCid =
	INLINE_UID(0x3C41D7F2, 0xB86547A0, 0x91E4CC26, 0x7AB53D19);

inline constexpr char kVendor[] = "EqualizerAPO-XT contributors";
inline constexpr char kUrl[] = "https://github.com/115dkk/EqualizerAPO-XT";
inline constexpr char kEmail[] = "";
inline constexpr char kPluginName[] = "EAPO XT Subwoofer Routing";
inline constexpr char kControllerName[] = "EAPO XT Subwoofer Routing Controller";
inline constexpr char kVersion[] = "1.0.0";
inline constexpr char kSdkVersion[] = "VST 3.8";
inline constexpr char kSubCategories[] = "Fx|Tools";

inline constexpr char kParameterMessageId[] = "eapo-xt-subwoofer-routing-parameter";
inline constexpr char kMessageParameterId[] = "parameter-id";
inline constexpr char kMessageParameterValue[] = "normalized-value";

// Processor -> controller: the rate setupProcessing() accepted, so the
// controller's headroom preview compiles at the rate the audio runs at.
inline constexpr char kSampleRateMessageId[] = "eapo-xt-subwoofer-routing-sample-rate";
inline constexpr char kMessageSampleRate[] = "sample-rate";

}
