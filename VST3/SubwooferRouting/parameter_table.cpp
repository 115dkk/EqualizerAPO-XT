// SPDX-License-Identifier: MIT

#include "parameter_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <optional>
#include <utility>

namespace eapoxt::subwooferrouting::vst3
{
namespace
{

constexpr std::uint32_t kStateMagic = 0x31584D42; // "BMX1" in little-endian byte order.
constexpr std::uint32_t kMaximumStateBytes = 64u * 1024u * 1024u;

std::optional<std::size_t> findSourceLfePath(
	const subroute::SubwooferRoutingState& state) noexcept
{
	for (std::size_t index = 0; index < state.paths.size(); ++index)
	{
		if (state.paths[index].kind == subroute::PathKind::SourceLfe)
			return index;
	}
	return std::nullopt;
}

bool readBypass(
	const subroute::SubwooferRoutingState&,
	double,
	bool bypass,
	double& plain) noexcept
{
	plain = bypass ? 1.0 : 0.0;
	return true;
}

bool writeBypass(
	subroute::SubwooferRoutingState&,
	double plain,
	double,
	bool& bypass) noexcept
{
	bypass = plain >= 0.5;
	return true;
}

bool readGain(
	const subroute::SubwooferRoutingState& state,
	double,
	bool,
	double& plain) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;
	plain = state.paths[*path].preGainDb;
	return true;
}

bool writeGain(
	subroute::SubwooferRoutingState& state,
	double plain,
	double,
	bool&) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;
	state.paths[*path].preGainDb = plain;
	return true;
}

bool readPolarity(
	const subroute::SubwooferRoutingState& state,
	double,
	bool,
	double& plain) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;

	for (const subroute::PathStage& stage : state.paths[*path].chain)
	{
		if (const subroute::PolarityStage* polarity =
			std::get_if<subroute::PolarityStage>(&stage))
		{
			plain = polarity->inverted ? 1.0 : 0.0;
			return true;
		}
	}
	return false;
}

bool writePolarity(
	subroute::SubwooferRoutingState& state,
	double plain,
	double,
	bool&) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;

	for (subroute::PathStage& stage : state.paths[*path].chain)
	{
		if (subroute::PolarityStage* polarity =
			std::get_if<subroute::PolarityStage>(&stage))
		{
			polarity->inverted = plain >= 0.5;
			return true;
		}
	}
	return false;
}

bool readDelay(
	const subroute::SubwooferRoutingState& state,
	double,
	bool,
	double& plain) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;

	for (const subroute::PathStage& stage : state.paths[*path].chain)
	{
		if (const subroute::DelayStage* delay =
			std::get_if<subroute::DelayStage>(&stage))
		{
			plain = delay->milliseconds;
			return true;
		}
	}
	return false;
}

bool writeDelay(
	subroute::SubwooferRoutingState& state,
	double plain,
	double,
	bool&) noexcept
{
	const std::optional<std::size_t> path = findSourceLfePath(state);
	if (!path.has_value())
		return false;

	for (subroute::PathStage& stage : state.paths[*path].chain)
	{
		if (subroute::DelayStage* delay =
			std::get_if<subroute::DelayStage>(&stage))
		{
			delay->milliseconds = plain;
			return true;
		}
	}
	return false;
}

bool readOutputTrim(
	const subroute::SubwooferRoutingState& state,
	double automaticTrimDb,
	bool,
	double& plain) noexcept
{
	plain = state.headroom.mode == subroute::HeadroomMode::Auto
		? automaticTrimDb
		: state.headroom.manualTrimDb;
	return true;
}

bool writeOutputTrim(
	subroute::SubwooferRoutingState& state,
	double plain,
	double automaticTrimDb,
	bool&) noexcept
{
	state.headroom.manualTrimDb = plain;
	if (std::fabs(plain - automaticTrimDb) > 1.0e-9)
		state.headroom.mode = subroute::HeadroomMode::Manual;
	return true;
}

bool readHeadroomAuto(
	const subroute::SubwooferRoutingState& state,
	double,
	bool,
	double& plain) noexcept
{
	plain = state.headroom.mode == subroute::HeadroomMode::Auto ? 1.0 : 0.0;
	return true;
}

bool writeHeadroomAuto(
	subroute::SubwooferRoutingState& state,
	double plain,
	double,
	bool&) noexcept
{
	state.headroom.mode = plain >= 0.5
		? subroute::HeadroomMode::Auto
		: subroute::HeadroomMode::Manual;
	return true;
}

constexpr std::array<ParameterDescriptor, kParameterCount> kParameters = {{
	{kBypassParamId, 0, 0.0, 1.0, L"", L"Bypass", L"Bypass", ParameterKind::Bypass, readBypass, writeBypass},
	{kSourceLfeGainParamId, 1, -20.0, 20.0, L"dB", L"Source LFE Gain", L"LFE Gain", ParameterKind::Gain, readGain, writeGain},
	{kSourceLfePolarityParamId, 2, 0.0, 1.0, L"", L"Source LFE Polarity", L"LFE Pol", ParameterKind::Polarity, readPolarity, writePolarity},
	{kSourceLfeDelayParamId, 3, 0.0, 100.0, L"ms", L"Source LFE Delay", L"LFE Delay", ParameterKind::Delay, readDelay, writeDelay},
	{kOutputTrimParamId, 4, -40.0, 0.0, L"dB", L"Global Output Trim", L"Trim", ParameterKind::OutputTrim, readOutputTrim, writeOutputTrim},
	{kHeadroomAutoParamId, 5, 0.0, 1.0, L"", L"Headroom Auto", L"Headroom", ParameterKind::HeadroomAuto, readHeadroomAuto, writeHeadroomAuto}
}};

bool isSwitch(const ParameterDescriptor& parameter) noexcept
{
	return parameter.kind == ParameterKind::Bypass
		|| parameter.kind == ParameterKind::Polarity
		|| parameter.kind == ParameterKind::HeadroomAuto;
}

}

const ParameterDescriptor* parameterById(std::uint32_t id) noexcept
{
	for (const ParameterDescriptor& parameter : kParameters)
	{
		if (parameter.id == id)
			return &parameter;
	}
	return nullptr;
}

const ParameterDescriptor* parameterBySlot(std::size_t slot) noexcept
{
	return slot < kParameters.size() ? &kParameters[slot] : nullptr;
}

double clampNormalizedParameter(double value) noexcept
{
	if (!std::isfinite(value))
		return 0.0;
	return std::clamp(value, 0.0, 1.0);
}

double normalizedParameterToPlain(
	const ParameterDescriptor& parameter,
	double normalized) noexcept
{
	const double clamped = clampNormalizedParameter(normalized);
	if (isSwitch(parameter))
		return clamped >= 0.5 ? 1.0 : 0.0;
	return parameter.minimum + clamped * (parameter.maximum - parameter.minimum);
}

double plainParameterToNormalized(
	const ParameterDescriptor& parameter,
	double plain) noexcept
{
	if (isSwitch(parameter))
		return plain >= 0.5 ? 1.0 : 0.0;
	return clampNormalizedParameter(
		(plain - parameter.minimum) / (parameter.maximum - parameter.minimum));
}

bool readNormalizedParameter(
	const ParameterDescriptor& parameter,
	const subroute::SubwooferRoutingState& state,
	double automaticTrimDb,
	bool bypass,
	double& normalized) noexcept
{
	double plain = parameter.minimum;
	if (parameter.read == nullptr
		|| !parameter.read(state, automaticTrimDb, bypass, plain))
	{
		return false;
	}
	normalized = plainParameterToNormalized(parameter, plain);
	return true;
}

bool writeNormalizedParameter(
	const ParameterDescriptor& parameter,
	subroute::SubwooferRoutingState& state,
	double normalized,
	double automaticTrimDb,
	bool& bypass) noexcept
{
	return parameter.write != nullptr && parameter.write(
		state,
		normalizedParameterToPlain(parameter, normalized),
		automaticTrimDb,
		bypass);
}

std::wstring displayParameterValue(
	const ParameterDescriptor& parameter,
	double normalized)
{
	const double plain = normalizedParameterToPlain(parameter, normalized);
	switch (parameter.kind)
	{
	case ParameterKind::Bypass:
		return plain >= 0.5 ? L"On" : L"Off";
	case ParameterKind::Polarity:
		return plain >= 0.5 ? L"Inverted" : L"Normal";
	case ParameterKind::HeadroomAuto:
		return plain >= 0.5 ? L"Auto" : L"Manual";
	case ParameterKind::Gain:
	case ParameterKind::OutputTrim:
	case ParameterKind::Delay:
		break;
	}

	wchar_t value[128] = {};
	std::swprintf(
		value,
		std::size(value),
		parameter.kind == ParameterKind::Delay ? L"%.2f ms" : L"%.2f dB",
		plain);
	return value;
}

bool readStateFrame(const StateFrameReader& read, std::string& json)
{
	if (!read)
		return false;

	std::uint32_t header[2] = {};
	if (!read(header, sizeof(header))
		|| header[0] != kStateMagic
		|| header[1] > kMaximumStateBytes)
	{
		return false;
	}

	std::string incoming(header[1], '\0');
	if (header[1] != 0 && !read(incoming.data(), incoming.size()))
		return false;

	json = std::move(incoming);
	return true;
}

bool writeStateFrame(const StateFrameWriter& write, const std::string& json)
{
	if (!write || json.size() > kMaximumStateBytes)
		return false;

	const std::uint32_t header[2] = {
		kStateMagic,
		static_cast<std::uint32_t>(json.size())
	};
	return write(header, sizeof(header))
		&& (json.empty() || write(json.data(), json.size()));
}

}
