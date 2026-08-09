/*
	This file is part of EqualizerAPO-XT.

	Subwoofer routing: the SubwooferRouting card descriptors over the
	state JSON the engine and the VST3 plugin exchange, and the crossover
	recipe layer (BW/LR recognition, Q tables, apply/recognize round trip)
	over the #246 built-in preset.
*/

#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <QString>
#include <QStringList>

#include "Editor/widgets/FilterCardModel.h"
#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"

#include "EditorLogicTestSupport.h"

void testSubwooferRoutingDescriptors()
{
	// The fixture is the built-in preset: card summaries must work for the
	// exact JSON the engine and the VST3 plugin exchange. Widget-level card
	// editor coverage lives in the Editor build itself (moc) because this
	// binary intentionally compiles no Q_OBJECT editor sources.
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	requireTrue(preset.succeeded(), "subwoofer-routing preset fixture created");
	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(*preset.state);
	requireTrue(encoded.succeeded(), "subwoofer-routing descriptor fixture encoded");

	const QString json = QString::fromUtf8(encoded.text->data(),
		static_cast<int>(encoded.text->size()));
	const FilterCardDescriptor descriptor = FilterCardModel::describeLine(
		QStringLiteral("SubwooferRouting: State ") + json);
	expectEqual(descriptor.type, "subwooferrouting", "subwoofer-routing card type");
	expectEqual(descriptor.badge, "SUB", "subwoofer-routing badge");
	expectEqual(descriptor.title, "Subwoofer routing", "subwoofer-routing title");
	expectFalse(descriptor.color.isEmpty(), "subwoofer-routing color is populated");
	expectFalse(descriptor.summary.isEmpty(), "subwoofer-routing state summary is populated");
	// Review round 2: the header speaks the user's language - the layout and
	// the crossover corner - instead of internal graph statistics.
	expectTrue(descriptor.summary.contains(QStringLiteral("4.1")),
		QStringLiteral("state summary names the layout: ") + descriptor.summary);
	expectTrue(descriptor.summary.contains(QStringLiteral("80 Hz")),
		QStringLiteral("state summary names the crossover corner: ") + descriptor.summary);

	const FilterCardDescriptor profile = FilterCardModel::describeLine(
		"SubwooferRouting: Profile \"Living Room.swxt.json\"");
	expectTrue(profile.summary.contains("Living Room.swxt.json"),
		QStringLiteral("profile summary names the linked file: ") + profile.summary);

	const FilterCardDescriptor broken = FilterCardModel::describeLine(
		"SubwooferRouting: State {\"schema\":\"wrong\"}");
	expectEqual(broken.type, "subwooferrouting", "invalid state keeps the card type");
	expectFalse(broken.summary.isEmpty(), "invalid state still has a summary");

	expectEqual(FilterCardModel::badgeIconResource("subwooferrouting", "SUB"),
		":/icons/modern/subwoofer-routing.svg", "subwoofer-routing badge icon");
	expectEqual(FilterCardModel::commandIconResource("SubwooferRouting"),
		":/icons/modern/subwoofer-routing.svg", "subwoofer-routing command icon");
	expectEqual(FilterCardModel::canonicalCommand("SubwooferRouting"),
		"SubwooferRouting", "subwoofer-routing canonical command");
}

void testSubwooferRoutingCrossoverRecipes()
{
	// The recipe layer names the alignments practitioners dial in (BW/LR at
	// even orders) over raw biquad chains. The #246 preset is the parity
	// fixture: its front speakers are the original config's single 80 Hz
	// Q 0.707 section (BW2), its front bass path the cascaded pair (LR4).
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	requireTrue(preset.succeeded(), "crossover preset fixture created");
	const subroute::SubwooferRoutingState& state = *preset.state;

	const auto pathById = [&state](const char* id) -> const subroute::Path*
	{
		for (const subroute::Path& path : state.paths)
		{
			if (path.id == id)
				return &path;
		}
		return nullptr;
	};

	const subroute::Path* frontLeft = pathById("FrontL");
	requireTrue(frontLeft != nullptr, "preset has FrontL");
	const std::optional<subroute::CrossoverRecipe> frontHp =
		subroute::recognizeCrossover(*frontLeft, subroute::BiquadType::HighPass);
	requireTrue(frontHp.has_value(), "FrontL high-pass recognized");
	expectEqual(QString::fromStdString(subroute::crossoverRecipeLabel(*frontHp)),
		"BW2", "FrontL is the original config's single Q 0.707 section");
	expectTrue(std::abs(frontHp->frequencyHz - 80.0) < 0.01,
		"FrontL corner is 80 Hz");

	const subroute::Path* frontBass = pathById("FrontBass");
	requireTrue(frontBass != nullptr, "preset has FrontBass");
	const std::optional<subroute::CrossoverRecipe> frontLp =
		subroute::recognizeCrossover(*frontBass, subroute::BiquadType::LowPass);
	requireTrue(frontLp.has_value(), "FrontBass low-pass recognized");
	expectEqual(QString::fromStdString(subroute::crossoverRecipeLabel(*frontLp)),
		"LR4", "FrontBass is the original config's cascaded Q 0.707 pair");

	// Q tables: every supported order, both alignments, exact section counts.
	expectEqual(static_cast<int>(subroute::crossoverSectionQs(
		subroute::CrossoverAlignment::Butterworth, 6).size()), 3,
		"BW6 has three sections");
	expectEqual(static_cast<int>(subroute::crossoverSectionQs(
		subroute::CrossoverAlignment::LinkwitzRiley, 6).size()), 3,
		"LR6 has three sections");
	expectTrue(subroute::crossoverSectionQs(
		subroute::CrossoverAlignment::Butterworth, 3).empty(),
		"odd orders are outside the vocabulary");
	const std::vector<double> lr6 = subroute::crossoverSectionQs(
		subroute::CrossoverAlignment::LinkwitzRiley, 6);
	expectTrue(std::abs(lr6[0] - 0.5) < 1e-9 && std::abs(lr6[1] - 1.0) < 1e-9,
		"LR6 factors as the squared odd Butterworth half");

	// Apply/recognize round-trip: rewriting FrontBass as LR8 at 60 Hz must
	// read back as exactly that, and the chain keeps its one delay stage.
	subroute::Path rewritten = *frontBass;
	subroute::CrossoverRecipe lr8;
	lr8.alignment = subroute::CrossoverAlignment::LinkwitzRiley;
	lr8.order = 8;
	lr8.frequencyHz = 60.0;
	requireTrue(subroute::applyCrossoverRecipe(
		rewritten, subroute::BiquadType::LowPass, lr8),
		"LR8 recipe applies");
	const std::optional<subroute::CrossoverRecipe> readBack =
		subroute::recognizeCrossover(rewritten, subroute::BiquadType::LowPass);
	requireTrue(readBack.has_value(), "rewritten chain recognized");
	expectEqual(QString::fromStdString(subroute::crossoverRecipeLabel(*readBack)),
		"LR8", "round-trip keeps the alignment");
	expectTrue(std::abs(readBack->frequencyHz - 60.0) < 0.01,
		"round-trip keeps the corner");
	int delayStages = 0;
	for (const subroute::PathStage& stage : rewritten.chain)
	{
		if (std::holds_alternative<subroute::DelayStage>(stage))
			delayStages++;
	}
	expectEqual(delayStages, 1, "rewrite preserves the single delay stage");

	// A split-frequency chain is deliberate custom work and must not be
	// claimed by any recipe.
	subroute::Path custom = *frontBass;
	for (subroute::PathStage& stage : custom.chain)
	{
		subroute::BiquadStage* biquad =
			std::get_if<subroute::BiquadStage>(&stage);
		if (biquad != nullptr
			&& biquad->filter.type == subroute::BiquadType::LowPass)
		{
			biquad->filter.frequencyHz = 95.0;
			break;
		}
	}
	expectFalse(subroute::recognizeCrossover(
		custom, subroute::BiquadType::LowPass).has_value(),
		"split-frequency chains stay custom");
}
