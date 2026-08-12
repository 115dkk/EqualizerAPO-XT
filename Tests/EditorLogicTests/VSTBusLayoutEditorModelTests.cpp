#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTBusLayoutEditorModel.h"

void testVSTBusLayoutEditorModel()
{
	VSTBusLayoutEditorModel empty;
	expectFalse(empty.contract().has_value(), QStringLiteral("VST bus editor starts without an explicit contract"));
	expectEqual(static_cast<int>(empty.input()), static_cast<int>(VST3BusLayout::Auto),
		QStringLiteral("missing VST input contract presents Auto"));
	expectEqual(static_cast<int>(empty.output()), static_cast<int>(VST3BusLayout::Auto),
		QStringLiteral("missing VST output contract presents Auto"));

	empty.setLayouts(VST3BusLayout::Stereo, VST3BusLayout::Surround71);
	expectTrue(empty.contract().has_value(), QStringLiteral("editing either selector establishes the paired contract"));
	expectEqual(static_cast<int>(empty.input()), static_cast<int>(VST3BusLayout::Stereo),
		QStringLiteral("VST input selection is retained"));
	expectEqual(static_cast<int>(empty.output()), static_cast<int>(VST3BusLayout::Surround71),
		QStringLiteral("VST output selection is retained"));

	empty.setLayouts(VST3BusLayout::Auto, VST3BusLayout::Auto);
	expectTrue(empty.contract().has_value(),
		QStringLiteral("an intentional Auto/Auto edit remains an explicit paired contract"));
	empty.removeLayouts();
	expectFalse(empty.contract().has_value(), QStringLiteral("remove clears both VST layout keys together"));

	VSTBusLayoutEditorModel migrated(std::nullopt, true);
	expectTrue(migrated.contract().has_value(), QStringLiteral("legacy StereoInput migrates to a bus contract"));
	expectTrue(migrated.migratedLegacyStereoInput(), QStringLiteral("legacy migration remains visible to the UI"));
	expectEqual(static_cast<int>(migrated.input()), static_cast<int>(VST3BusLayout::Stereo),
		QStringLiteral("legacy StereoInput migrates to Input Stereo"));
	expectEqual(static_cast<int>(migrated.output()), static_cast<int>(VST3BusLayout::Auto),
		QStringLiteral("legacy StereoInput migrates to Output Auto"));

	const VST3BusContract explicitContract{ VST3BusLayout::Mono, VST3BusLayout::Surround51 };
	VSTBusLayoutEditorModel explicitWins(explicitContract, true);
	expectFalse(explicitWins.migratedLegacyStereoInput(),
		QStringLiteral("explicit Input/Output wins over a conflicting legacy flag"));
	expectEqual(static_cast<int>(explicitWins.input()), static_cast<int>(VST3BusLayout::Mono),
		QStringLiteral("explicit VST input survives conflict"));
	expectEqual(static_cast<int>(explicitWins.output()), static_cast<int>(VST3BusLayout::Surround51),
		QStringLiteral("explicit VST output survives conflict"));
}
