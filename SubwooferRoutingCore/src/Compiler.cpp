// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Compiler.h"
#include <numbers>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace subroute
{

namespace
{

constexpr double kMaximumDelaySeconds = 10.0;

void addDiagnostic(
	ValidationResult& result,
	DiagnosticSeverity severity,
	DiagnosticCode code,
	std::string entityId,
	std::string jsonPointer,
	std::string message)
{
	ValidationDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.entityId = std::move(entityId);
	diagnostic.jsonPointer = std::move(jsonPointer);
	diagnostic.message = std::move(message);
	result.diagnostics.push_back(std::move(diagnostic));
}

bool isContinuationByte(unsigned char value) noexcept
{
	return (value & 0xc0U) == 0x80U;
}

bool isValidUtf8(std::string_view text) noexcept
{
	std::size_t index = 0;

	while (index < text.size())
	{
		const auto first = static_cast<unsigned char>(text[index]);

		if (first <= 0x7fU)
		{
			++index;
			continue;
		}

		if (first >= 0xc2U && first <= 0xdfU)
		{
			if (index + 1 >= text.size()
				|| !isContinuationByte(static_cast<unsigned char>(text[index + 1])))
			{
				return false;
			}

			index += 2;
			continue;
		}

		if (first >= 0xe0U && first <= 0xefU)
		{
			if (index + 2 >= text.size())
			{
				return false;
			}

			const auto second = static_cast<unsigned char>(text[index + 1]);
			const auto third = static_cast<unsigned char>(text[index + 2]);

			if (!isContinuationByte(second) || !isContinuationByte(third))
			{
				return false;
			}

			if ((first == 0xe0U && second < 0xa0U)
				|| (first == 0xedU && second >= 0xa0U))
			{
				return false;
			}

			index += 3;
			continue;
		}

		if (first >= 0xf0U && first <= 0xf4U)
		{
			if (index + 3 >= text.size())
			{
				return false;
			}

			const auto second = static_cast<unsigned char>(text[index + 1]);
			const auto third = static_cast<unsigned char>(text[index + 2]);
			const auto fourth = static_cast<unsigned char>(text[index + 3]);

			if (!isContinuationByte(second)
				|| !isContinuationByte(third)
				|| !isContinuationByte(fourth))
			{
				return false;
			}

			if ((first == 0xf0U && second < 0x90U)
				|| (first == 0xf4U && second >= 0x90U))
			{
				return false;
			}

			index += 4;
			continue;
		}

		return false;
	}

	return true;
}

std::string indexedPointer(const char* collection, std::size_t index)
{
	return std::string("/") + collection + "/" + std::to_string(index);
}

void validateUtf8Field(
	ValidationResult& result,
	const std::string& value,
	const std::string& entityId,
	const std::string& pointer)
{
	if (!isValidUtf8(value))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidUtf8,
			entityId,
			pointer,
			"The string is not valid UTF-8.");
	}
}

void validateStableIdField(
	ValidationResult& result,
	const std::string& value,
	const std::string& entityId,
	const std::string& pointer)
{
	if (!isValidStableId(value))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidStableId,
			entityId,
			pointer,
			"The stable ID does not match [A-Za-z0-9][A-Za-z0-9._-]*.");
	}
}

void validateGain(
	ValidationResult& result,
	double value,
	const std::string& entityId,
	const std::string& pointer)
{
	if (!std::isfinite(value))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::NonFiniteGain,
			entityId,
			pointer,
			"The gain must be finite.");
	}
}

void validateFilter(
	ValidationResult& result,
	const BiquadFilter& filter,
	const std::string& entityId,
	const std::string& pointer)
{
	if (!std::isfinite(filter.frequencyHz))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::NonFiniteFrequency,
			entityId,
			pointer + "/frequencyHz",
			"The filter frequency must be finite.");
	}
	else if (filter.frequencyHz <= 0.0)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidFrequency,
			entityId,
			pointer + "/frequencyHz",
			"The filter frequency must be greater than zero.");
	}

	if (!std::isfinite(filter.q))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::NonFiniteQ,
			entityId,
			pointer + "/q",
			"The filter Q or shelf slope must be finite.");
	}
	else if (filter.q <= 0.0
		|| ((filter.type == BiquadType::LowShelf
			|| filter.type == BiquadType::HighShelf)
			&& filter.q > 1.0))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidQ,
			entityId,
			pointer + "/q",
			"The filter Q must be positive and an RBJ shelf slope must be in (0, 1].");
	}

	validateGain(result, filter.gainDb, entityId, pointer + "/gainDb");
}

void forEachFilter(
	const SubwooferRoutingState& state,
	const std::function<void(const BiquadFilter&, const std::string&, const std::string&)>& callback)
{
	for (std::size_t pathIndex = 0; pathIndex < state.paths.size(); ++pathIndex)
	{
		const Path& path = state.paths[pathIndex];
		const std::string pathPointer = indexedPointer("paths", pathIndex);

		for (std::size_t stageIndex = 0; stageIndex < path.chain.size(); ++stageIndex)
		{
			const PathStage& stage = path.chain[stageIndex];
			const std::string stagePointer =
				pathPointer + "/chain/" + std::to_string(stageIndex);

			if (const auto* biquad = std::get_if<BiquadStage>(&stage))
			{
				callback(biquad->filter, path.id, stagePointer + "/filter");
			}
			else if (const auto* slotsStage = std::get_if<EqualizerSlotsStage>(&stage))
			{
				for (std::size_t filterIndex = 0;
					filterIndex < slotsStage->filters.size();
					++filterIndex)
				{
					callback(
						slotsStage->filters[filterIndex],
						path.id,
						stagePointer + "/filters/" + std::to_string(filterIndex));
				}
			}
		}
	}
}

bool pathDependencyGraphHasCycle(const SubwooferRoutingState& state)
{
	// Paths only consume physical inputs, so the dependency lists are empty in schema version 1.
	std::vector<std::vector<std::size_t>> dependencies(state.paths.size());

	enum class VisitState
	{
		Unvisited,
		Visiting,
		Visited
	};

	std::vector<VisitState> visits(state.paths.size(), VisitState::Unvisited);

	std::function<bool(std::size_t)> visit = [&](std::size_t pathIndex)
	{
		if (visits[pathIndex] == VisitState::Visiting)
		{
			return true;
		}

		if (visits[pathIndex] == VisitState::Visited)
		{
			return false;
		}

		visits[pathIndex] = VisitState::Visiting;

		for (const std::size_t dependency : dependencies[pathIndex])
		{
			if (visit(dependency))
			{
				return true;
			}
		}

		visits[pathIndex] = VisitState::Visited;
		return false;
	};

	for (std::size_t pathIndex = 0; pathIndex < state.paths.size(); ++pathIndex)
	{
		if (visit(pathIndex))
		{
			return true;
		}
	}

	return false;
}

double decibelsToLinear(double decibels)
{
	return std::pow(10.0, decibels / 20.0);
}

double linearToDecibels(double linear)
{
	if (linear <= 0.0)
	{
		return -std::numeric_limits<double>::infinity();
	}

	return 20.0 * std::log10(linear);
}

BiquadCoefficients makeBiquad(
	const BiquadFilter& filter,
	double sampleRate)
{
	const double omega = 2.0 * std::numbers::pi_v<double> * filter.frequencyHz / sampleRate;
	const double cosine = std::cos(omega);
	const double sine = std::sin(omega);
	const double amplitude = std::pow(10.0, filter.gainDb / 40.0);

	double b0 = 1.0;
	double b1 = 0.0;
	double b2 = 0.0;
	double a0 = 1.0;
	double a1 = 0.0;
	double a2 = 0.0;

	switch (filter.type)
	{
	case BiquadType::HighPass:
	{
		const double alpha = sine / (2.0 * filter.q);
		b0 = (1.0 + cosine) / 2.0;
		b1 = -(1.0 + cosine);
		b2 = (1.0 + cosine) / 2.0;
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosine;
		a2 = 1.0 - alpha;
		break;
	}

	case BiquadType::LowPass:
	{
		const double alpha = sine / (2.0 * filter.q);
		b0 = (1.0 - cosine) / 2.0;
		b1 = 1.0 - cosine;
		b2 = (1.0 - cosine) / 2.0;
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosine;
		a2 = 1.0 - alpha;
		break;
	}

	case BiquadType::Peaking:
	{
		const double alpha = sine / (2.0 * filter.q);
		b0 = 1.0 + alpha * amplitude;
		b1 = -2.0 * cosine;
		b2 = 1.0 - alpha * amplitude;
		a0 = 1.0 + alpha / amplitude;
		a1 = -2.0 * cosine;
		a2 = 1.0 - alpha / amplitude;
		break;
	}

	case BiquadType::LowShelf:
	{
		const double alpha = sine / 2.0
			* std::sqrt(
				(amplitude + 1.0 / amplitude) * (1.0 / filter.q - 1.0)
				+ 2.0);
		const double twoRootAmplitudeAlpha =
			2.0 * std::sqrt(amplitude) * alpha;

		b0 = amplitude
			* ((amplitude + 1.0)
				- (amplitude - 1.0) * cosine
				+ twoRootAmplitudeAlpha);
		b1 = 2.0 * amplitude
			* ((amplitude - 1.0)
				- (amplitude + 1.0) * cosine);
		b2 = amplitude
			* ((amplitude + 1.0)
				- (amplitude - 1.0) * cosine
				- twoRootAmplitudeAlpha);
		a0 = (amplitude + 1.0)
			+ (amplitude - 1.0) * cosine
			+ twoRootAmplitudeAlpha;
		a1 = -2.0
			* ((amplitude - 1.0)
				+ (amplitude + 1.0) * cosine);
		a2 = (amplitude + 1.0)
			+ (amplitude - 1.0) * cosine
			- twoRootAmplitudeAlpha;
		break;
	}

	case BiquadType::HighShelf:
	{
		const double alpha = sine / 2.0
			* std::sqrt(
				(amplitude + 1.0 / amplitude) * (1.0 / filter.q - 1.0)
				+ 2.0);
		const double twoRootAmplitudeAlpha =
			2.0 * std::sqrt(amplitude) * alpha;

		b0 = amplitude
			* ((amplitude + 1.0)
				+ (amplitude - 1.0) * cosine
				+ twoRootAmplitudeAlpha);
		b1 = -2.0 * amplitude
			* ((amplitude - 1.0)
				+ (amplitude + 1.0) * cosine);
		b2 = amplitude
			* ((amplitude + 1.0)
				+ (amplitude - 1.0) * cosine
				- twoRootAmplitudeAlpha);
		a0 = (amplitude + 1.0)
			- (amplitude - 1.0) * cosine
			+ twoRootAmplitudeAlpha;
		a1 = 2.0
			* ((amplitude - 1.0)
				- (amplitude + 1.0) * cosine);
		a2 = (amplitude + 1.0)
			- (amplitude - 1.0) * cosine
			- twoRootAmplitudeAlpha;
		break;
	}

	case BiquadType::Notch:
	{
		const double alpha = sine / (2.0 * filter.q);
		b0 = 1.0;
		b1 = -2.0 * cosine;
		b2 = 1.0;
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosine;
		a2 = 1.0 - alpha;
		break;
	}

	case BiquadType::AllPass:
	{
		const double alpha = sine / (2.0 * filter.q);
		b0 = 1.0 - alpha;
		b1 = -2.0 * cosine;
		b2 = 1.0 + alpha;
		a0 = 1.0 + alpha;
		a1 = -2.0 * cosine;
		a2 = 1.0 - alpha;
		break;
	}
	}

	BiquadCoefficients coefficients;
	coefficients.b0 = b0 / a0;
	coefficients.b1 = b1 / a0;
	coefficients.b2 = b2 / a0;
	coefficients.a1 = a1 / a0;
	coefficients.a2 = a2 / a0;
	return coefficients;
}

void appendGainStage(
	std::vector<CompiledStage>& stages,
	double gainLinear)
{
	if (!stages.empty()
		&& stages.back().kind == CompiledStageKind::Gain)
	{
		stages.back().gainLinear *= gainLinear;
		return;
	}

	CompiledStage stage;
	stage.kind = CompiledStageKind::Gain;
	stage.gainLinear = gainLinear;
	stages.push_back(stage);
}

void appendDelayStage(
	std::vector<CompiledStage>& stages,
	double milliseconds,
	double sampleRate)
{
	if (milliseconds == 0.0)
	{
		return;
	}

	double samples = milliseconds * sampleRate / 1000.0;
	const double nearestInteger = std::round(samples);
	const double tolerance =
		16.0 * std::numeric_limits<double>::epsilon()
		* std::max(1.0, std::abs(samples));

	if (std::abs(samples - nearestInteger) <= tolerance)
	{
		samples = nearestInteger;
	}

	double integerPart = std::floor(samples);
	double fractionalPart = samples - integerPart;

	if (fractionalPart < tolerance)
	{
		fractionalPart = 0.0;
	}
	else if (1.0 - fractionalPart < tolerance)
	{
		integerPart += 1.0;
		fractionalPart = 0.0;
	}

	CompiledStage stage;
	stage.kind = CompiledStageKind::Delay;
	stage.integerDelaySamples = static_cast<std::size_t>(integerPart);
	stage.fractionalDelaySamples = fractionalPart;
	stages.push_back(stage);
}

void appendBiquadStage(
	std::vector<CompiledStage>& stages,
	const BiquadFilter& filter,
	double sampleRate)
{
	CompiledStage stage;
	stage.kind = CompiledStageKind::Biquad;
	stage.biquad = makeBiquad(filter, sampleRate);
	stages.push_back(stage);
}

std::complex<double> evaluateBiquad(
	const BiquadCoefficients& coefficients,
	double omega)
{
	const std::complex<double> z1 = std::polar(1.0, -omega);
	const std::complex<double> z2 = z1 * z1;

	const std::complex<double> numerator =
		coefficients.b0
		+ coefficients.b1 * z1
		+ coefficients.b2 * z2;
	const std::complex<double> denominator =
		1.0
		+ coefficients.a1 * z1
		+ coefficients.a2 * z2;

	return numerator / denominator;
}

std::complex<double> evaluatePathResponse(
	const CompiledPath& path,
	double frequencyHz,
	double sampleRate)
{
	const double omega = 2.0 * std::numbers::pi_v<double> * frequencyHz / sampleRate;
	std::complex<double> response(1.0, 0.0);

	for (const CompiledStage& stage : path.stages)
	{
		switch (stage.kind)
		{
		case CompiledStageKind::Gain:
			response *= stage.gainLinear;
			break;

		case CompiledStageKind::Delay:
		{
			const double delay =
				static_cast<double>(stage.integerDelaySamples)
				+ stage.fractionalDelaySamples;
			response *= std::polar(1.0, -omega * delay);
			break;
		}

		case CompiledStageKind::Biquad:
			response *= evaluateBiquad(stage.biquad, omega);
			break;
		}
	}

	return response;
}

HeadroomAnalysis analyzeHeadroom(
	const SubwooferRoutingState& state,
	const PrepareSpec& prepareSpec,
	const std::vector<CompiledPath>& paths,
	const std::vector<CompiledOutput>& outputs)
{
	HeadroomAnalysis analysis;
	analysis.mode = state.headroom.mode;
	analysis.frequencySampleCount = kHeadroomFrequencySampleCount;
	analysis.outputs.reserve(outputs.size());

	for (const CompiledOutput& output : outputs)
	{
		OutputHeadroomResult outputResult;
		outputResult.outputChannelId = output.targetChannelId;
		outputResult.criticalFrequencyHz = kHeadroomMinimumFrequencyHz;
		analysis.outputs.push_back(std::move(outputResult));
	}

	const double nyquist = prepareSpec.sampleRate / 2.0;
	std::vector<std::complex<double>> pathResponses(paths.size());
	std::vector<std::complex<double>> inputTransfers(
		prepareSpec.channelLayout.size());

	bool globalCriticalSet = false;

	for (std::size_t frequencyIndex = 0;
		frequencyIndex < kHeadroomFrequencySampleCount;
		++frequencyIndex)
	{
		const double interpolation =
			kHeadroomFrequencySampleCount == 1
			? 0.0
			: static_cast<double>(frequencyIndex)
				/ static_cast<double>(kHeadroomFrequencySampleCount - 1);
		const double frequency =
			kHeadroomMinimumFrequencyHz
			* std::exp(
				std::log(nyquist / kHeadroomMinimumFrequencyHz)
				* interpolation);

		for (std::size_t pathIndex = 0; pathIndex < paths.size(); ++pathIndex)
		{
			pathResponses[pathIndex] =
				evaluatePathResponse(
					paths[pathIndex],
					frequency,
					prepareSpec.sampleRate);
		}

		for (std::size_t outputIndex = 0; outputIndex < outputs.size(); ++outputIndex)
		{
			const CompiledOutput& output = outputs[outputIndex];
			std::fill(
				inputTransfers.begin(),
				inputTransfers.end(),
				std::complex<double>(0.0, 0.0));

			if (output.mode == OutputMode::Add)
			{
				inputTransfers[output.targetChannelIndex] +=
					std::complex<double>(1.0, 0.0);
			}

			for (const CompiledOutputTerm& outputTerm : output.terms)
			{
				const CompiledPath& path = paths[outputTerm.sourcePathIndex];
				const std::complex<double> pathContribution =
					outputTerm.gainLinear
					* pathResponses[outputTerm.sourcePathIndex];

				for (const CompiledSourceTerm& sourceTerm : path.sourceMix)
				{
					inputTransfers[sourceTerm.inputChannelIndex] +=
						pathContribution * sourceTerm.gainLinear;
				}
			}

			double predictedMagnitude = 0.0;

			for (const std::complex<double>& transfer : inputTransfers)
			{
				predictedMagnitude += std::abs(transfer);
			}

			OutputHeadroomResult& outputResult = analysis.outputs[outputIndex];

			if (frequencyIndex == 0
				|| predictedMagnitude > outputResult.predictedPeakLinearBeforeTrim)
			{
				outputResult.predictedPeakLinearBeforeTrim = predictedMagnitude;
				outputResult.predictedPeakDbBeforeTrim =
					linearToDecibels(predictedMagnitude);
				outputResult.criticalFrequencyHz = frequency;
			}

			if (!globalCriticalSet
				|| predictedMagnitude > analysis.predictedPeakLinearBeforeTrim)
			{
				globalCriticalSet = true;
				analysis.predictedPeakLinearBeforeTrim = predictedMagnitude;
				analysis.predictedPeakDbBeforeTrim =
					linearToDecibels(predictedMagnitude);
				analysis.criticalOutputChannelId = output.targetChannelId;
				analysis.criticalFrequencyHz = frequency;
			}
		}
	}

	if (analysis.mode == HeadroomMode::Auto)
	{
		if (analysis.predictedPeakLinearBeforeTrim > 0.0)
		{
			analysis.appliedTrimDb = std::min(
				0.0,
				-linearToDecibels(analysis.predictedPeakLinearBeforeTrim));
		}
		else
		{
			analysis.appliedTrimDb = 0.0;
		}
	}
	else
	{
		analysis.appliedTrimDb = state.headroom.manualTrimDb;
	}

	analysis.appliedTrimLinear = decibelsToLinear(analysis.appliedTrimDb);
	return analysis;
}

}

bool ValidationResult::hasErrors() const noexcept
{
	return std::any_of(
		diagnostics.begin(),
		diagnostics.end(),
		[](const ValidationDiagnostic& diagnostic)
		{
			return diagnostic.severity == DiagnosticSeverity::Error;
		});
}

bool ValidationResult::hasWarnings() const noexcept
{
	return std::any_of(
		diagnostics.begin(),
		diagnostics.end(),
		[](const ValidationDiagnostic& diagnostic)
		{
			return diagnostic.severity == DiagnosticSeverity::Warning;
		});
}

bool ValidationResult::succeeded() const noexcept
{
	return !hasErrors();
}

bool CompileResult::succeeded() const noexcept
{
	return graph.has_value() && !validation.hasErrors();
}

bool isValidStableId(std::string_view id) noexcept
{
	if (id.empty())
	{
		return false;
	}

	const auto isAlphaNumeric = [](char value)
	{
		return (value >= 'A' && value <= 'Z')
			|| (value >= 'a' && value <= 'z')
			|| (value >= '0' && value <= '9');
	};

	if (!isAlphaNumeric(id.front()))
	{
		return false;
	}

	for (const char value : id)
	{
		if (!isAlphaNumeric(value)
			&& value != '.'
			&& value != '_'
			&& value != '-')
		{
			return false;
		}
	}

	return true;
}

ValidationResult validate(const SubwooferRoutingState& state)
{
	ValidationResult result;

	validateUtf8Field(result, state.schema, std::string(), "/schema");

	if (state.schema != kSubwooferRoutingSchema)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidSchema,
			std::string(),
			"/schema",
			"The state schema is not the subwoofer-routing schema.");
	}

	if (state.version != kSubwooferRoutingSchemaVersion)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::UnsupportedSchemaVersion,
			std::string(),
			"/version",
			"The state schema version is unsupported.");
	}

	if (state.layout.channels.empty())
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::EmptyPhysicalLayout,
			std::string(),
			"/layout/channels",
			"The physical channel layout must not be empty.");
	}

	std::unordered_set<std::string> physicalChannelIds;

	for (std::size_t channelIndex = 0;
		channelIndex < state.layout.channels.size();
		++channelIndex)
	{
		const PhysicalChannel& channel = state.layout.channels[channelIndex];
		const std::string pointer =
			"/layout/channels/" + std::to_string(channelIndex);

		validateUtf8Field(result, channel.id, channel.id, pointer + "/id");
		validateUtf8Field(
			result,
			channel.displayName,
			channel.id,
			pointer + "/displayName");
		validateStableIdField(result, channel.id, channel.id, pointer + "/id");

		if (!physicalChannelIds.insert(channel.id).second)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::DuplicatePhysicalChannelId,
				channel.id,
				pointer + "/id",
				"The physical channel ID is duplicated.");
		}
	}

	if (state.paths.empty())
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::NoPaths,
			std::string(),
			"/paths",
			"The state must contain at least one path.");
	}

	std::unordered_map<std::string, std::size_t> pathIndices;

	for (std::size_t pathIndex = 0; pathIndex < state.paths.size(); ++pathIndex)
	{
		const Path& path = state.paths[pathIndex];
		const std::string pointer = indexedPointer("paths", pathIndex);

		validateUtf8Field(result, path.id, path.id, pointer + "/id");
		validateStableIdField(result, path.id, path.id, pointer + "/id");

		if (!pathIndices.emplace(path.id, pathIndex).second)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::DuplicatePathId,
				path.id,
				pointer + "/id",
				"The path ID is duplicated.");
		}

		validateGain(result, path.preGainDb, path.id, pointer + "/preGainDb");
		validateGain(result, path.postGainDb, path.id, pointer + "/postGainDb");

		if (path.sourceMix.empty())
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::PathHasNoInputs,
				path.id,
				pointer + "/sourceMix",
				"The path source mix must contain at least one input.");
		}

		for (std::size_t termIndex = 0;
			termIndex < path.sourceMix.size();
			++termIndex)
		{
			const SourceMixTerm& term = path.sourceMix[termIndex];
			const std::string termPointer =
				pointer + "/sourceMix/" + std::to_string(termIndex);

			validateUtf8Field(
				result,
				term.inputChannelId,
				path.id,
				termPointer + "/inputChannelId");
			validateStableIdField(
				result,
				term.inputChannelId,
				path.id,
				termPointer + "/inputChannelId");
			validateGain(
				result,
				term.gainLinear,
				path.id,
				termPointer + "/gainLinear");

			if (!physicalChannelIds.contains(term.inputChannelId))
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::UnknownPhysicalInputChannel,
					path.id,
					termPointer + "/inputChannelId",
					"The source mix references a channel outside the state layout.");
			}
		}

		std::size_t polarityCount = 0;
		std::size_t delayCount = 0;
		std::size_t equalizerSlotsCount = 0;

		for (std::size_t stageIndex = 0;
			stageIndex < path.chain.size();
			++stageIndex)
		{
			const PathStage& stage = path.chain[stageIndex];
			const std::string stagePointer =
				pointer + "/chain/" + std::to_string(stageIndex);

			if (const auto* gain = std::get_if<GainStage>(&stage))
			{
				validateGain(
					result,
					gain->gainDb,
					path.id,
					stagePointer + "/gainDb");
			}
			else if (std::get_if<PolarityStage>(&stage) != nullptr)
			{
				++polarityCount;
			}
			else if (const auto* delay = std::get_if<DelayStage>(&stage))
			{
				++delayCount;

				if (!std::isfinite(delay->milliseconds))
				{
					addDiagnostic(
						result,
						DiagnosticSeverity::Error,
						DiagnosticCode::NonFiniteDelay,
						path.id,
						stagePointer + "/milliseconds",
						"The delay must be finite.");
				}
				else if (delay->milliseconds < 0.0)
				{
					addDiagnostic(
						result,
						DiagnosticSeverity::Error,
						DiagnosticCode::NegativeDelay,
						path.id,
						stagePointer + "/milliseconds",
						"The delay must not be negative.");
				}
			}
			else if (const auto* biquad = std::get_if<BiquadStage>(&stage))
			{
				validateFilter(
					result,
					biquad->filter,
					path.id,
					stagePointer + "/filter");
			}
			else if (const auto* slotsStage = std::get_if<EqualizerSlotsStage>(&stage))
			{
				++equalizerSlotsCount;

				for (std::size_t filterIndex = 0;
					filterIndex < slotsStage->filters.size();
					++filterIndex)
				{
					validateFilter(
						result,
						slotsStage->filters[filterIndex],
						path.id,
						stagePointer + "/filters/" + std::to_string(filterIndex));
				}
			}
		}

		if (polarityCount == 0)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MissingPolarityStage,
				path.id,
				pointer + "/chain",
				"The path must contain exactly one polarity stage.");
		}
		else if (polarityCount > 1)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MultiplePolarityStages,
				path.id,
				pointer + "/chain",
				"The path contains more than one polarity stage.");
		}

		if (delayCount == 0)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MissingDelayStage,
				path.id,
				pointer + "/chain",
				"The path must contain exactly one delay stage.");
		}
		else if (delayCount > 1)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MultipleDelayStages,
				path.id,
				pointer + "/chain",
				"The path contains more than one delay stage.");
		}

		if (equalizerSlotsCount == 0)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MissingEqualizerSlotsStage,
				path.id,
				pointer + "/chain",
				"The path must contain exactly one equalizer-slots stage.");
		}
		else if (equalizerSlotsCount > 1)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MultipleEqualizerSlotsStages,
				path.id,
				pointer + "/chain",
				"The path contains more than one equalizer-slots stage.");
		}
	}

	std::unordered_set<std::string> groupIds;
	std::unordered_set<std::string> groupReferencedPathIds;

	for (std::size_t groupIndex = 0;
		groupIndex < state.speakerGroups.size();
		++groupIndex)
	{
		const SpeakerGroup& group = state.speakerGroups[groupIndex];
		const std::string pointer =
			indexedPointer("speakerGroups", groupIndex);

		validateUtf8Field(result, group.id, group.id, pointer + "/id");
		validateUtf8Field(
			result,
			group.displayName,
			group.id,
			pointer + "/displayName");
		validateStableIdField(result, group.id, group.id, pointer + "/id");

		if (!groupIds.insert(group.id).second)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::DuplicateSpeakerGroupId,
				group.id,
				pointer + "/id",
				"The speaker-group ID is duplicated.");
		}

		if (group.mainPathIds.empty() && !group.bassPathId.has_value())
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::EmptySpeakerGroup,
				group.id,
				pointer,
				"The speaker group does not reference a main or bass path.");
		}

		std::unordered_set<std::string> localReferences;

		for (std::size_t referenceIndex = 0;
			referenceIndex < group.mainPathIds.size();
			++referenceIndex)
		{
			const std::string& pathId = group.mainPathIds[referenceIndex];
			const std::string referencePointer =
				pointer + "/mainPathIds/" + std::to_string(referenceIndex);

			validateUtf8Field(result, pathId, group.id, referencePointer);
			validateStableIdField(result, pathId, group.id, referencePointer);
			groupReferencedPathIds.insert(pathId);

			if (!localReferences.insert(pathId).second)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DuplicateSpeakerGroupPathReference,
					group.id,
					referencePointer,
					"The speaker group references the same path more than once.");
			}

			const auto pathIt = pathIndices.find(pathId);

			if (pathIt == pathIndices.end())
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DanglingMainPathReference,
					group.id,
					referencePointer,
					"The main-path reference does not resolve.");
			}
			else if (state.paths[pathIt->second].kind != PathKind::Main)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::PathKindMismatch,
					pathId,
					referencePointer,
					"A main-path reference must resolve to a main path.");
			}
		}

		if (group.bassPathId.has_value())
		{
			const std::string& pathId = *group.bassPathId;
			const std::string referencePointer = pointer + "/bassPathId";

			validateUtf8Field(result, pathId, group.id, referencePointer);
			validateStableIdField(result, pathId, group.id, referencePointer);
			groupReferencedPathIds.insert(pathId);

			if (!localReferences.insert(pathId).second)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DuplicateSpeakerGroupPathReference,
					group.id,
					referencePointer,
					"The speaker group references the same path more than once.");
			}

			const auto pathIt = pathIndices.find(pathId);

			if (pathIt == pathIndices.end())
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DanglingBassPathReference,
					group.id,
					referencePointer,
					"The bass-path reference does not resolve.");
			}
			else if (state.paths[pathIt->second].kind != PathKind::Bass)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::PathKindMismatch,
					pathId,
					referencePointer,
					"A bass-path reference must resolve to a bass path.");
			}
		}
	}

	if (state.outputMatrix.empty())
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::OutputMatrixHasNoOutputs,
			std::string(),
			"/outputMatrix",
			"The output matrix must contain at least one output.");
	}

	std::unordered_set<std::string> outputTargets;
	std::unordered_set<std::string> matrixReferencedPathIds;

	for (std::size_t outputIndex = 0;
		outputIndex < state.outputMatrix.size();
		++outputIndex)
	{
		const OutputMatrixEntry& output = state.outputMatrix[outputIndex];
		const std::string pointer =
			indexedPointer("outputMatrix", outputIndex);

		validateUtf8Field(
			result,
			output.targetChannelId,
			output.targetChannelId,
			pointer + "/targetChannelId");
		validateStableIdField(
			result,
			output.targetChannelId,
			output.targetChannelId,
			pointer + "/targetChannelId");

		if (!outputTargets.insert(output.targetChannelId).second)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::DuplicateOutputTarget,
				output.targetChannelId,
				pointer + "/targetChannelId",
				"The output target is duplicated.");
		}

		if (!physicalChannelIds.contains(output.targetChannelId))
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::UnknownPhysicalOutputChannel,
				output.targetChannelId,
				pointer + "/targetChannelId",
				"The output target is outside the state layout.");
		}

		if (output.terms.empty())
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::OutputHasNoTerms,
				output.targetChannelId,
				pointer + "/terms",
				"The output matrix entry must contain at least one term.");
		}

		for (std::size_t termIndex = 0;
			termIndex < output.terms.size();
			++termIndex)
		{
			const OutputMatrixTerm& term = output.terms[termIndex];
			const std::string termPointer =
				pointer + "/terms/" + std::to_string(termIndex);

			validateUtf8Field(
				result,
				term.sourcePathId,
				output.targetChannelId,
				termPointer + "/sourcePathId");
			validateStableIdField(
				result,
				term.sourcePathId,
				output.targetChannelId,
				termPointer + "/sourcePathId");
			validateGain(
				result,
				term.gainDb,
				output.targetChannelId,
				termPointer + "/gainDb");

			matrixReferencedPathIds.insert(term.sourcePathId);

			if (!pathIndices.contains(term.sourcePathId))
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DanglingOutputPathReference,
					output.targetChannelId,
					termPointer + "/sourcePathId",
					"The output-matrix path reference does not resolve.");
			}
		}
	}

	if (!std::isfinite(state.headroom.manualTrimDb))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::NonFiniteHeadroomTrim,
			std::string(),
			"/headroom/manualTrimDb",
			"The manual headroom trim must be finite.");
	}
	else if (state.headroom.mode == HeadroomMode::Manual
		&& state.headroom.manualTrimDb > 0.0)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::PositiveManualTrim,
			std::string(),
			"/headroom/manualTrimDb",
			"The manual headroom trim must not be positive.");
	}

	validateUtf8Field(
		result,
		state.metadata.profileName,
		std::string(),
		"/metadata/profileName");
	validateUtf8Field(
		result,
		state.metadata.creatingApp,
		std::string(),
		"/metadata/creatingApp");
	validateUtf8Field(
		result,
		state.metadata.creatingAppVersion,
		std::string(),
		"/metadata/creatingAppVersion");

	for (const Path& path : state.paths)
	{
		const bool matrixReferenced =
			matrixReferencedPathIds.contains(path.id);
		const bool groupReferenced =
			groupReferencedPathIds.contains(path.id);

		if (path.kind == PathKind::SourceLfe)
		{
			if (!matrixReferenced)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Warning,
					DiagnosticCode::SourceLfePathNotReferenced,
					path.id,
					"/paths",
					"The source-LFE path is not referenced by the output matrix.");
			}
		}
		else if (!groupReferenced || !matrixReferenced)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Warning,
				DiagnosticCode::UnusedPath,
				path.id,
				"/paths",
				"The path is not fully connected through a speaker group and output.");
		}
	}

	for (std::size_t groupIndex = 0;
		groupIndex < state.speakerGroups.size();
		++groupIndex)
	{
		const SpeakerGroup& group = state.speakerGroups[groupIndex];
		bool used = false;

		for (const std::string& pathId : group.mainPathIds)
		{
			used = used
				|| matrixReferencedPathIds.contains(pathId);
		}

		if (group.bassPathId.has_value())
		{
			used = used
				|| matrixReferencedPathIds.contains(*group.bassPathId);
		}

		if (!used)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Warning,
				DiagnosticCode::UnusedSpeakerGroup,
				group.id,
				indexedPointer("speakerGroups", groupIndex),
				"The speaker group has no path referenced by the output matrix.");
		}
	}

	if (pathDependencyGraphHasCycle(state))
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::GraphCycle,
			std::string(),
			"/paths",
			"The path dependency graph contains a cycle.");
	}

	return result;
}

ValidationResult validate(
	const SubwooferRoutingState& state,
	const PrepareSpec& prepareSpec)
{
	ValidationResult result = validate(state);

	const bool validSampleRate =
		std::isfinite(prepareSpec.sampleRate)
		&& prepareSpec.sampleRate > 0.0;

	if (!validSampleRate)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidSampleRate,
			std::string(),
			"/prepareSpec/sampleRate",
			"The sample rate must be finite and greater than zero.");
	}

	if (prepareSpec.maximumBlockSize == 0)
	{
		addDiagnostic(
			result,
			DiagnosticSeverity::Error,
			DiagnosticCode::InvalidMaximumBlockSize,
			std::string(),
			"/prepareSpec/maximumBlockSize",
			"The maximum block size must be greater than zero.");
	}

	std::unordered_set<std::string> deviceChannelIds;

	for (std::size_t channelIndex = 0;
		channelIndex < prepareSpec.channelLayout.size();
		++channelIndex)
	{
		const std::string& channelId = prepareSpec.channelLayout[channelIndex];

		if (!deviceChannelIds.insert(channelId).second)
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::DuplicateDeviceChannel,
				channelId,
				"/prepareSpec/channelLayout/" + std::to_string(channelIndex),
				"The current device channel layout contains a duplicate ID.");
		}
	}

	for (std::size_t channelIndex = 0;
		channelIndex < state.layout.channels.size();
		++channelIndex)
	{
		const std::string& channelId = state.layout.channels[channelIndex].id;

		if (!deviceChannelIds.contains(channelId))
		{
			addDiagnostic(
				result,
				DiagnosticSeverity::Error,
				DiagnosticCode::MissingDeviceChannel,
				channelId,
				"/layout/channels/" + std::to_string(channelIndex) + "/id",
				"The state channel is not available on the current device.");
		}
	}

	if (validSampleRate)
	{
		const double nyquist = prepareSpec.sampleRate / 2.0;

		forEachFilter(
			state,
			[&](const BiquadFilter& filter,
				const std::string& entityId,
				const std::string& pointer)
			{
				if (std::isfinite(filter.frequencyHz)
					&& filter.frequencyHz >= nyquist)
				{
					addDiagnostic(
						result,
						DiagnosticSeverity::Error,
						DiagnosticCode::FrequencyAtOrAboveNyquist,
						entityId,
						pointer + "/frequencyHz",
						"The filter frequency must be below Nyquist.");
				}
			});
	}

	for (std::size_t pathIndex = 0; pathIndex < state.paths.size(); ++pathIndex)
	{
		const Path& path = state.paths[pathIndex];

		for (std::size_t stageIndex = 0;
			stageIndex < path.chain.size();
			++stageIndex)
		{
			const auto* delay = std::get_if<DelayStage>(&path.chain[stageIndex]);

			if (delay != nullptr
				&& std::isfinite(delay->milliseconds)
				&& delay->milliseconds > kMaximumDelaySeconds * 1000.0)
			{
				addDiagnostic(
					result,
					DiagnosticSeverity::Error,
					DiagnosticCode::DelayTooLarge,
					path.id,
					"/paths/" + std::to_string(pathIndex)
						+ "/chain/" + std::to_string(stageIndex)
						+ "/milliseconds",
					"The delay exceeds the ten-second compiled delay-line limit.");
			}
		}
	}

	return result;
}

CompileResult compile(
	const SubwooferRoutingState& state,
	const PrepareSpec& prepareSpec)
{
	CompileResult result;
	result.validation = validate(state, prepareSpec);

	if (result.validation.hasErrors())
	{
		return result;
	}

	std::unordered_map<std::string, std::size_t> deviceChannelIndices;

	for (std::size_t channelIndex = 0;
		channelIndex < prepareSpec.channelLayout.size();
		++channelIndex)
	{
		deviceChannelIndices.emplace(
			prepareSpec.channelLayout[channelIndex],
			channelIndex);
	}

	std::vector<CompiledPath> compiledPaths;
	compiledPaths.reserve(state.paths.size());

	for (const Path& path : state.paths)
	{
		CompiledPath compiledPath;
		compiledPath.id = path.id;
		compiledPath.kind = path.kind;
		compiledPath.sourceMix.reserve(path.sourceMix.size());

		for (const SourceMixTerm& sourceTerm : path.sourceMix)
		{
			CompiledSourceTerm compiledTerm;
			compiledTerm.inputChannelIndex =
				deviceChannelIndices.at(sourceTerm.inputChannelId);
			compiledTerm.gainLinear = sourceTerm.gainLinear;
			compiledPath.sourceMix.push_back(compiledTerm);
		}

		appendGainStage(
			compiledPath.stages,
			decibelsToLinear(path.preGainDb));

		for (const PathStage& stage : path.chain)
		{
			if (const auto* gain = std::get_if<GainStage>(&stage))
			{
				appendGainStage(
					compiledPath.stages,
					decibelsToLinear(gain->gainDb));
			}
			else if (const auto* polarity = std::get_if<PolarityStage>(&stage))
			{
				if (polarity->inverted)
				{
					appendGainStage(compiledPath.stages, -1.0);
				}
			}
			else if (const auto* delay = std::get_if<DelayStage>(&stage))
			{
				appendDelayStage(
					compiledPath.stages,
					delay->milliseconds,
					prepareSpec.sampleRate);
			}
			else if (const auto* biquad = std::get_if<BiquadStage>(&stage))
			{
				appendBiquadStage(
					compiledPath.stages,
					biquad->filter,
					prepareSpec.sampleRate);
			}
			else if (const auto* slotsStage = std::get_if<EqualizerSlotsStage>(&stage))
			{
				for (const BiquadFilter& filter : slotsStage->filters)
				{
					appendBiquadStage(
						compiledPath.stages,
						filter,
						prepareSpec.sampleRate);
				}
			}
		}

		appendGainStage(
			compiledPath.stages,
			decibelsToLinear(path.postGainDb));

		compiledPaths.push_back(std::move(compiledPath));
	}

	std::unordered_map<std::string, std::size_t> compiledPathIndices;

	for (std::size_t pathIndex = 0;
		pathIndex < compiledPaths.size();
		++pathIndex)
	{
		compiledPathIndices.emplace(compiledPaths[pathIndex].id, pathIndex);
	}

	std::vector<CompiledOutput> compiledOutputs;
	compiledOutputs.reserve(state.outputMatrix.size());

	for (const OutputMatrixEntry& output : state.outputMatrix)
	{
		CompiledOutput compiledOutput;
		compiledOutput.targetChannelId = output.targetChannelId;
		compiledOutput.targetChannelIndex =
			deviceChannelIndices.at(output.targetChannelId);
		compiledOutput.mode = output.mode;
		compiledOutput.terms.reserve(output.terms.size());

		for (const OutputMatrixTerm& term : output.terms)
		{
			CompiledOutputTerm compiledTerm;
			compiledTerm.sourcePathIndex =
				compiledPathIndices.at(term.sourcePathId);
			compiledTerm.gainLinear = decibelsToLinear(term.gainDb);
			compiledOutput.terms.push_back(compiledTerm);
		}

		compiledOutputs.push_back(std::move(compiledOutput));
	}

	HeadroomAnalysis analysis = analyzeHeadroom(
		state,
		prepareSpec,
		compiledPaths,
		compiledOutputs);

	if (analysis.mode == HeadroomMode::Auto
		&& analysis.appliedTrimDb < 0.0)
	{
		addDiagnostic(
			result.validation,
			DiagnosticSeverity::Warning,
			DiagnosticCode::AutomaticHeadroomApplied,
			analysis.criticalOutputChannelId,
			"/headroom",
			"Automatic headroom trim was applied.");
	}
	else if (analysis.mode == HeadroomMode::Manual
		&& analysis.predictedPeakLinearBeforeTrim
			* analysis.appliedTrimLinear > 1.0)
	{
		addDiagnostic(
			result.validation,
			DiagnosticSeverity::Warning,
			DiagnosticCode::ManualHeadroomMayClip,
			analysis.criticalOutputChannelId,
			"/headroom/manualTrimDb",
			"The manual trim may leave the predicted output above unity.");
	}

	result.headroom = analysis;
	result.graph.emplace(
		prepareSpec,
		std::move(compiledPaths),
		std::move(compiledOutputs),
		std::move(analysis));
	return result;
}

}
