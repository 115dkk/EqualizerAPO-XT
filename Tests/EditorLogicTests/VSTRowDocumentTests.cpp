/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTRowDocument.h"

void testVSTRowDocument()
{
	const std::vector<std::wstring> stereoFill = {L"L", L"R"};
	const std::vector<std::wstring> surroundFill = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};

	// The fill lists arrive with the contract they belong to.
	{
		VSTRowDocument document(VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Surround51}, false,
			stereoFill, surroundFill);
		expectTrue(document.fill().railPresent(false) && document.fill().railPresent(true),
			QStringLiteral("the document hands its contract to the fill"));
		expectEqual(static_cast<int>(document.fill().inputFill().size()), 2,
			QStringLiteral("the input fill arrives with the document"));

		// Changing the input layout clears only the input fill: the output
		// side's slot count did not change.
		document.setLayouts(VST3BusLayout::Surround51, VST3BusLayout::Surround51);
		expectTrue(document.fill().inputFill().empty(), QStringLiteral("a changed input layout clears the input fill"));
		expectEqual(static_cast<int>(document.fill().outputFill().size()), 6,
			QStringLiteral("an unchanged output layout keeps the output fill"));
		expectEqual(document.fill().slotCount(false), 6,
			QStringLiteral("the fill follows the new input layout"));

		// Setting the same layouts again changes nothing.
		document.pickSlot(false, 0, L"C");
		document.setLayouts(VST3BusLayout::Surround51, VST3BusLayout::Surround51);
		expectFalse(document.fill().inputFill().empty(), QStringLiteral("unchanged layouts keep both fills"));

		// Clearing the layouts removes the contract and both fill lists.
		document.clearLayouts();
		expectFalse(document.bus().contract().has_value(), QStringLiteral("clearing the layouts removes the contract"));
		expectTrue(document.fill().inputFill().empty() && document.fill().outputFill().empty(),
			QStringLiteral("clearing the layouts clears both fills"));
		expectFalse(document.fill().railPresent(false) || document.fill().railPresent(true),
			QStringLiteral("without a contract there are no rails"));
	}

	// Changing only the output layout clears only the output fill.
	{
		VSTRowDocument document(VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Surround51}, false,
			stereoFill, surroundFill);
		document.setLayouts(VST3BusLayout::Stereo, VST3BusLayout::Stereo);
		expectEqual(static_cast<int>(document.fill().inputFill().size()), 2,
			QStringLiteral("an unchanged input layout keeps the input fill"));
		expectTrue(document.fill().outputFill().empty(), QStringLiteral("a changed output layout clears the output fill"));
	}

	// clearFill returns both sides to the implicit default and keeps the
	// contract.
	{
		VSTRowDocument document(VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Surround51}, false,
			stereoFill, surroundFill);
		document.clearFill();
		expectTrue(document.fill().sideDefaulted(false) && document.fill().sideDefaulted(true),
			QStringLiteral("clearFill defaults both sides"));
		expectTrue(document.bus().contract().has_value(), QStringLiteral("clearFill keeps the contract"));
	}

	// The engine's resolver decides what is missing: a position number and
	// the SL alias of RL resolve under a selection that contains the
	// matching channels, so neither is marked; a channel the selection does
	// not contain is.
	{
		VSTRowDocument document(VST3BusContract{VST3BusLayout::Surround51, VST3BusLayout::Auto}, false,
			{L"1", L"SL", L"C", L"-", L"-", L"-"}, {});
		document.setSelectedChannels({L"L", L"R", L"RL", L"RR"});
		expectFalse(document.fill().slotChannelMissing(false, 0),
			QStringLiteral("a position number inside the selection is not missing"));
		expectFalse(document.fill().slotChannelMissing(false, 1),
			QStringLiteral("SL resolves onto RL in the selection"));
		expectTrue(document.fill().slotChannelMissing(false, 2),
			QStringLiteral("a channel outside the selection is missing"));
	}

	// The legacy StereoInput flag migrates into the equivalent contract when
	// asked to, and stays out of the document otherwise.
	{
		const VSTRowDocument migrated(std::nullopt, true, {}, {});
		expectTrue(migrated.bus().migratedLegacyStereoInput(), QStringLiteral("StereoInput migrates on request"));
		expectTrue(migrated.fill().railPresent(false), QStringLiteral("the migrated Stereo input has a rail"));
		const VSTRowDocument kept(std::nullopt, false, {}, {});
		expectFalse(kept.bus().contract().has_value(), QStringLiteral("without the flag there is no contract"));
	}
}
