#pragma once

#include <cstdint>

namespace testvst3
{
constexpr uint32_t stateMagic = 0x33545354u; // TST3

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
