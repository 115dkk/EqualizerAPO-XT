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

inline constexpr Steinberg::Vst::ParamID kBypassParamId = 1000;
inline constexpr Steinberg::Vst::ParamID kSourceLfeGainParamId = 1001;
inline constexpr Steinberg::Vst::ParamID kSourceLfePolarityParamId = 1002;
inline constexpr Steinberg::Vst::ParamID kSourceLfeDelayParamId = 1003;
inline constexpr Steinberg::Vst::ParamID kOutputTrimParamId = 1004;
inline constexpr Steinberg::Vst::ParamID kHeadroomAutoParamId = 1005;

inline constexpr Steinberg::uint32 kStateMagic = 0x31584D42; // "BMX1" in little-endian byte order.
inline constexpr Steinberg::uint32 kMaximumStateBytes = 64u * 1024u * 1024u;

inline constexpr char kParameterMessageId[] = "eapo-xt-subwoofer-routing-parameter";
inline constexpr char kMessageParameterId[] = "parameter-id";
inline constexpr char kMessageParameterValue[] = "normalized-value";

}
