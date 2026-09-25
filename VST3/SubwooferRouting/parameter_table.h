// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "SubwooferRouting/State.h"

namespace eapoxt::subwooferrouting::vst3
{

inline constexpr std::size_t kParameterCount = 6;

inline constexpr std::uint32_t kBypassParamId = 1000;
inline constexpr std::uint32_t kSourceLfeGainParamId = 1001;
inline constexpr std::uint32_t kSourceLfePolarityParamId = 1002;
inline constexpr std::uint32_t kSourceLfeDelayParamId = 1003;
inline constexpr std::uint32_t kOutputTrimParamId = 1004;
inline constexpr std::uint32_t kHeadroomAutoParamId = 1005;

enum class ParameterKind
{
	Bypass,
	Gain,
	Polarity,
	Delay,
	OutputTrim,
	HeadroomAuto
};

using ParameterStateReader = bool (*)(
	const subroute::SubwooferRoutingState& state,
	double automaticTrimDb,
	bool bypass,
	double& plain) noexcept;

using ParameterStateWriter = bool (*)(
	subroute::SubwooferRoutingState& state,
	double plain,
	double automaticTrimDb,
	bool& bypass) noexcept;

struct ParameterDescriptor
{
	std::uint32_t id;
	std::size_t slot;
	double minimum;
	double maximum;
	const wchar_t* unit;
	const wchar_t* title;
	const wchar_t* shortTitle;
	ParameterKind kind;
	ParameterStateReader read;
	ParameterStateWriter write;
};

const ParameterDescriptor* parameterById(std::uint32_t id) noexcept;
const ParameterDescriptor* parameterBySlot(std::size_t slot) noexcept;

double clampNormalizedParameter(double value) noexcept;
double normalizedParameterToPlain(
	const ParameterDescriptor& parameter,
	double normalized) noexcept;
double plainParameterToNormalized(
	const ParameterDescriptor& parameter,
	double plain) noexcept;

bool readNormalizedParameter(
	const ParameterDescriptor& parameter,
	const subroute::SubwooferRoutingState& state,
	double automaticTrimDb,
	bool bypass,
	double& normalized) noexcept;

bool writeNormalizedParameter(
	const ParameterDescriptor& parameter,
	subroute::SubwooferRoutingState& state,
	double normalized,
	double automaticTrimDb,
	bool& bypass) noexcept;

std::wstring displayParameterValue(
	const ParameterDescriptor& parameter,
	double normalized);

using StateFrameReader = std::function<bool(void*, std::size_t)>;
using StateFrameWriter = std::function<bool(const void*, std::size_t)>;

bool readStateFrame(const StateFrameReader& read, std::string& json);
bool writeStateFrame(const StateFrameWriter& write, const std::string& json);

}
