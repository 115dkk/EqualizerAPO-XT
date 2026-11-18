#pragma once

#include <cstdint>

namespace testvst3
{
constexpr uint32_t stateMagic = 0x33545354u; // TST3

namespace mode
{
constexpr wchar_t floatOnly[] = L"FloatOnly.vst3";
constexpr wchar_t rejectInit[] = L"RejectInit.vst3";
constexpr wchar_t rejectComponent[] = L"RejectComponent.vst3";
constexpr wchar_t controllerState[] = L"ControllerState.vst3";
constexpr wchar_t busInfoMismatch[] = L"BusInfoMismatch.vst3";
constexpr wchar_t upmixer[] = L"Upmixer.vst3";
constexpr wchar_t declaredUpmixer[] = L"DeclaredUpmixer.vst3";
constexpr wchar_t rejectSetup[] = L"RejectSetup.vst3";
constexpr wchar_t rejectActive[] = L"RejectActive.vst3";
constexpr wchar_t rejectProcessing[] = L"RejectProcessing.vst3";
// setProcessing answers kNotImplemented, as the VST3 SDK's AudioEffect base
// class does, and still processes (LoudMax VST3 1.47, issue #478).
constexpr wchar_t unimplementedProcessing[] = L"UnimplementedProcessing.vst3";
constexpr wchar_t surround41CineOnly[] = L"Surround41CineOnly.vst3";
constexpr wchar_t surround41[] = L"Surround41.vst3";
constexpr wchar_t toneGenerator[] = L"ToneGenerator.vst3";
constexpr wchar_t latencyUpmixer[] = L"LatencyUpmixer.vst3";
constexpr wchar_t auxBusUpmixer[] = L"AuxBusUpmixer.vst3";
constexpr wchar_t retainHandler[] = L"RetainHandler.vst3";
constexpr wchar_t ignoreParameterFlush[] = L"IgnoreParameterFlush.vst3";
constexpr wchar_t faultProcess[] = L"FaultProcess.vst3";
constexpr wchar_t nanOutput[] = L"NanOutput.vst3";
constexpr wchar_t stateLatency[] = L"StateLatency.vst3";
constexpr wchar_t faultActive[] = L"FaultActive.vst3";
constexpr wchar_t faultState[] = L"FaultState.vst3";
constexpr wchar_t refuseState[] = L"RefuseState.vst3";
constexpr wchar_t fullName[] = L"FullName.vst3";
constexpr wchar_t spuriousDirty[] = L"SpuriousDirty.vst3";
constexpr wchar_t readOnlyMeter[] = L"ReadOnlyMeter.vst3";
constexpr wchar_t volatileState[] = L"VolatileState.vst3";
}

#pragma pack(push, 1)
struct PluginState
{
	uint32_t magic = stateMagic;
	double gain = 1.0;
};
#pragma pack(pop)

constexpr wchar_t flushEnteredEvent[] = L"Local\\EqualizerAPO_XT_TestVst3_FlushEntered";
constexpr wchar_t flushContinueEvent[] = L"Local\\EqualizerAPO_XT_TestVst3_FlushContinue";
constexpr wchar_t concurrentProcessingEvent[] = L"Local\\EqualizerAPO_XT_TestVst3_ConcurrentProcessing";
constexpr wchar_t invalidTerminateEvent[] = L"Local\\EqualizerAPO_XT_TestVst3_InvalidTerminate";
}
