// SPDX-License-Identifier: MIT

#pragma once

#include "State.h"

#include <optional>
#include <string>
#include <vector>

namespace subroute
{

/*
	Crossover recipes name the alignments practitioners actually dial in
	(Butterworth and Linkwitz-Riley at even acoustic orders) over the raw
	biquad-section vocabulary the state model stores. The recipe layer is
	how the editor, the summary cards and the VST3 parameter surface talk
	about "an LR4 at 80 Hz" without each reimplementing Q tables.

	Odd orders need a first-order section the state model does not carry,
	so they are outside the recipe vocabulary on purpose. A chain that does
	not match any recipe is simply custom - recognition returns nothing and
	editors must preserve the sections as written.
*/

enum class CrossoverAlignment
{
	Butterworth,
	LinkwitzRiley
};

struct CrossoverRecipe
{
	CrossoverAlignment alignment = CrossoverAlignment::Butterworth;
	// Acoustic order: 2, 4, 6 or 8 (slope = 6 * order dB/octave).
	int order = 2;
	double frequencyHz = 80.0;
};

/*
	The section Q sequence realizing the alignment at the given order.
	Empty when the order is unsupported (odd, or outside 2..8).
*/
std::vector<double> crossoverSectionQs(
	CrossoverAlignment alignment, int order);

/*
	The biquad sections realizing the recipe for HighPass or LowPass.
	Empty when the recipe or type is unsupported.
*/
std::vector<BiquadFilter> makeCrossoverSections(
	const CrossoverRecipe& recipe, BiquadType type);

/*
	Recognizes the sections of `type` in the path chain as one recipe: all
	sections must share one corner frequency and their Q multiset must match
	a known alignment table (both within a small relative tolerance, so the
	0.707 the original configs spell survives). Returns nothing for custom
	chains or when no section of the type exists.
*/
std::optional<CrossoverRecipe> recognizeCrossover(
	const Path& path, BiquadType type);

/*
	Replaces the sections of `type` in the path chain with the recipe
	realization. The first existing section of the type anchors the
	insertion position; with none present the sections are inserted before
	the DelayStage, or appended when the chain has no delay. Returns false
	when the recipe is unsupported (the chain is left untouched).
*/
bool applyCrossoverRecipe(
	Path& path, BiquadType type, const CrossoverRecipe& recipe);

// "BW2", "LR4", ... - empty for unsupported recipes.
std::string crossoverRecipeLabel(const CrossoverRecipe& recipe);

inline int crossoverSlopeDbPerOctave(const CrossoverRecipe& recipe)
{
	return 6 * recipe.order;
}

}
