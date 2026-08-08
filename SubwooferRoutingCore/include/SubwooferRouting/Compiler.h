// SPDX-License-Identifier: MIT

#pragma once

#include "Graph.h"
#include "State.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace subroute
{

enum class DiagnosticSeverity
{
	Warning,
	Error
};

enum class DiagnosticCode
{
	InvalidSchema,
	UnsupportedSchemaVersion,
	InvalidUtf8,
	InvalidStableId,
	EmptyPhysicalLayout,
	DuplicatePhysicalChannelId,
	DuplicatePathId,
	DuplicateSpeakerGroupId,
	DuplicateOutputTarget,
	DuplicateSpeakerGroupPathReference,
	EmptySpeakerGroup,
	NoPaths,
	PathHasNoInputs,
	OutputMatrixHasNoOutputs,
	OutputHasNoTerms,
	DanglingMainPathReference,
	DanglingBassPathReference,
	DanglingOutputPathReference,
	PathKindMismatch,
	UnknownPhysicalInputChannel,
	UnknownPhysicalOutputChannel,
	MissingDeviceChannel,
	DuplicateDeviceChannel,
	InvalidSampleRate,
	InvalidMaximumBlockSize,
	NonFiniteGain,
	NonFiniteDelay,
	NegativeDelay,
	DelayTooLarge,
	NonFiniteFrequency,
	InvalidFrequency,
	FrequencyAtOrAboveNyquist,
	NonFiniteQ,
	InvalidQ,
	NonFiniteHeadroomTrim,
	PositiveManualTrim,
	MissingPolarityStage,
	MultiplePolarityStages,
	MissingDelayStage,
	MultipleDelayStages,
	MissingEqualizerSlotsStage,
	MultipleEqualizerSlotsStages,
	GraphCycle,
	UnusedPath,
	UnusedSpeakerGroup,
	SourceLfePathNotReferenced,
	AutomaticHeadroomApplied,
	ManualHeadroomMayClip
};

struct ValidationDiagnostic
{
	DiagnosticSeverity severity = DiagnosticSeverity::Error;
	DiagnosticCode code = DiagnosticCode::InvalidSchema;
	std::string entityId;
	std::string jsonPointer;
	std::string message;
};

struct ValidationResult
{
	std::vector<ValidationDiagnostic> diagnostics;

	bool hasErrors() const noexcept;
	bool hasWarnings() const noexcept;
	bool succeeded() const noexcept;
};

struct CompileResult
{
	ValidationResult validation;
	std::optional<ProcessingGraph> graph;
	std::optional<HeadroomAnalysis> headroom;

	bool succeeded() const noexcept;
};

/*
	Stable IDs use the ASCII grammar:

		[A-Za-z0-9][A-Za-z0-9._-]*

	Display names and metadata are unrestricted valid UTF-8 strings.
*/
bool isValidStableId(std::string_view id) noexcept;

/*
	Performs all state-only validation, including schema/version, IDs,
	references, finite values, path structure, layout membership, matrix
	structure, and cycle checks. Sample-rate and current-device checks require
	the overload accepting PrepareSpec.
*/
ValidationResult validate(const SubwooferRoutingState& state);

/*
	Performs state-only validation plus PrepareSpec validation, Nyquist cutoff
	validation, and current-device channel availability checks.
*/
ValidationResult validate(
	const SubwooferRoutingState& state,
	const PrepareSpec& prepareSpec);

/*
	Compilation invokes the full PrepareSpec-aware validation. It produces no
	graph when any error diagnostic exists. Warnings do not prevent
	compilation.
*/
CompileResult compile(
	const SubwooferRoutingState& state,
	const PrepareSpec& prepareSpec);

}
