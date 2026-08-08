// SPDX-License-Identifier: MIT

#include "SubwooferRouting/Crossover.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace subroute
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

// Relative tolerance for recognizing written values ("0.707", "80") as the
// exact alignment constants.
constexpr double kRelativeTolerance = 5e-3;

bool nearlyEqual(double a, double b)
{
	const double scale = std::max(std::abs(a), std::abs(b));
	return std::abs(a - b) <= kRelativeTolerance * std::max(scale, 1e-9);
}

/*
	Butterworth pole-pair Qs for even order n:
	Q_k = 1 / (2 cos(theta_k)), theta_k = pi (2k + 1) / (2n).
*/
std::vector<double> butterworthQs(int order)
{
	std::vector<double> result;
	result.reserve(static_cast<std::size_t>(order / 2));
	for (int k = 0; k < order / 2; ++k)
	{
		const double theta =
			kPi * (2.0 * k + 1.0) / (2.0 * order);
		result.push_back(1.0 / (2.0 * std::cos(theta)));
	}
	return result;
}

bool isSupportedOrder(int order)
{
	return order == 2 || order == 4 || order == 6 || order == 8;
}

}

std::vector<double> crossoverSectionQs(
	CrossoverAlignment alignment, int order)
{
	if (!isSupportedOrder(order))
		return {};

	if (alignment == CrossoverAlignment::Butterworth)
		return butterworthQs(order);

	/*
		Linkwitz-Riley of order n is the squared Butterworth of order
		n / 2. Squaring turns the odd halves' first-order factor into one
		biquad at Q = 0.5 and doubles every pole-pair Q, so all four even
		LR orders stay all-biquad:

		LR2 = (1st)^2            -> {0.5}
		LR4 = BW2^2              -> {0.7071 x2}
		LR6 = (1st * Q1.0)^2     -> {0.5, 1.0 x2}
		LR8 = BW4^2              -> {0.5412 x2, 1.3066 x2}
	*/
	switch (order)
	{
	case 2:
		return {0.5};
	case 4:
	{
		const std::vector<double> half = butterworthQs(2);
		return {half[0], half[0]};
	}
	case 6:
		return {0.5, 1.0, 1.0};
	case 8:
	{
		const std::vector<double> half = butterworthQs(4);
		return {half[0], half[0], half[1], half[1]};
	}
	default:
		return {};
	}
}

std::vector<BiquadFilter> makeCrossoverSections(
	const CrossoverRecipe& recipe, BiquadType type)
{
	if (type != BiquadType::HighPass && type != BiquadType::LowPass)
		return {};
	if (!(recipe.frequencyHz > 0.0))
		return {};

	const std::vector<double> qs =
		crossoverSectionQs(recipe.alignment, recipe.order);
	std::vector<BiquadFilter> sections;
	sections.reserve(qs.size());
	for (const double q : qs)
	{
		BiquadFilter filter;
		filter.type = type;
		filter.frequencyHz = recipe.frequencyHz;
		filter.q = q;
		filter.gainDb = 0.0;
		sections.push_back(filter);
	}
	return sections;
}

std::optional<CrossoverRecipe> recognizeCrossover(
	const Path& path, BiquadType type)
{
	std::vector<double> qs;
	double frequencyHz = 0.0;

	for (const PathStage& stage : path.chain)
	{
		const BiquadStage* biquad =
			std::get_if<BiquadStage>(&stage);
		if (biquad == nullptr || biquad->filter.type != type)
			continue;

		if (qs.empty())
		{
			frequencyHz = biquad->filter.frequencyHz;
		}
		else if (!nearlyEqual(frequencyHz,
			biquad->filter.frequencyHz))
		{
			// Split-frequency chains are deliberate custom work.
			return std::nullopt;
		}
		qs.push_back(biquad->filter.q);
	}

	if (qs.empty())
		return std::nullopt;

	std::vector<double> sortedQs = qs;
	std::sort(sortedQs.begin(), sortedQs.end());

	const CrossoverAlignment alignments[] = {
		CrossoverAlignment::Butterworth,
		CrossoverAlignment::LinkwitzRiley
	};
	for (const CrossoverAlignment alignment : alignments)
	{
		for (int order = 2; order <= 8; order += 2)
		{
			std::vector<double> reference =
				crossoverSectionQs(alignment, order);
			if (reference.size() != sortedQs.size())
				continue;
			std::sort(reference.begin(), reference.end());

			bool matches = true;
			for (std::size_t index = 0;
				index < reference.size(); ++index)
			{
				if (!nearlyEqual(reference[index],
					sortedQs[index]))
				{
					matches = false;
					break;
				}
			}
			if (!matches)
				continue;

			// BW2 and LR4 share the multiset {0.7071} vs {0.7071 x2}
			// only across different sizes, so the first size match is
			// the recipe - except the ambiguous single-section Q 0.5,
			// which LR2 owns (a lone Butterworth section at Q 0.5 is
			// not a standard alignment).
			CrossoverRecipe recipe;
			recipe.alignment = alignment;
			recipe.order = order;
			recipe.frequencyHz = frequencyHz;
			return recipe;
		}
	}

	return std::nullopt;
}

bool applyCrossoverRecipe(
	Path& path, BiquadType type, const CrossoverRecipe& recipe)
{
	const std::vector<BiquadFilter> sections =
		makeCrossoverSections(recipe, type);
	if (sections.empty())
		return false;

	// Remove the existing sections of the type, remembering where the
	// first one sat so the rebuilt run keeps its place in the chain.
	std::size_t insertIndex = path.chain.size();
	bool sawSection = false;
	std::vector<PathStage> rebuilt;
	rebuilt.reserve(path.chain.size() + sections.size());
	for (PathStage& stage : path.chain)
	{
		const BiquadStage* biquad =
			std::get_if<BiquadStage>(&stage);
		if (biquad != nullptr && biquad->filter.type == type)
		{
			if (!sawSection)
			{
				insertIndex = rebuilt.size();
				sawSection = true;
			}
			continue;
		}
		rebuilt.push_back(std::move(stage));
	}

	if (!sawSection)
	{
		// No section yet: crossover filtering belongs before the path's
		// delay, mirroring the original configs' stage order.
		insertIndex = rebuilt.size();
		for (std::size_t index = 0; index < rebuilt.size(); ++index)
		{
			if (std::holds_alternative<DelayStage>(rebuilt[index]))
			{
				insertIndex = index;
				break;
			}
		}
	}

	std::vector<PathStage> stages;
	stages.reserve(sections.size());
	for (const BiquadFilter& filter : sections)
		stages.push_back(BiquadStage{filter});
	rebuilt.insert(rebuilt.begin()
			+ static_cast<std::ptrdiff_t>(insertIndex),
		stages.begin(), stages.end());
	path.chain = std::move(rebuilt);
	return true;
}

std::string crossoverRecipeLabel(const CrossoverRecipe& recipe)
{
	if (!isSupportedOrder(recipe.order))
		return {};

	const char* prefix =
		recipe.alignment == CrossoverAlignment::Butterworth
			? "BW"
			: "LR";
	return std::string(prefix) + std::to_string(recipe.order);
}

}
