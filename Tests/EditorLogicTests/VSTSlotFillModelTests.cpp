/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTSlotFillModel.h"

void testVSTSlotFillModel()
{
	const std::vector<std::wstring> surround = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};

	// No contract: no rails, no latch, nothing to edit.
	VSTSlotFillModel bare;
	bare.setSelectedChannels(surround);
	expectFalse(bare.railPresent(false) || bare.railPresent(true),
		QStringLiteral("a bare line has no fill rails"));
	expectFalse(bare.latchPresent(), QStringLiteral("no latch without rails"));

	// One explicit side: that rail exists, the latch does not (a single rail
	// never folds).
	VSTSlotFillModel oneSide;
	oneSide.setContract(VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Auto});
	oneSide.setSelectedChannels(surround);
	expectTrue(oneSide.railPresent(false), QStringLiteral("the explicit input side has a rail"));
	expectFalse(oneSide.railPresent(true), QStringLiteral("the Auto output side has none"));
	expectFalse(oneSide.latchPresent(), QStringLiteral("a single rail carries no fold latch"));

	// Both sides explicit: both rails and the latch.
	VSTSlotFillModel both;
	both.setContract(VST3BusContract{VST3BusLayout::Surround51, VST3BusLayout::Stereo});
	both.setSelectedChannels(surround);
	expectTrue(both.railPresent(false) && both.railPresent(true) && both.latchPresent(),
		QStringLiteral("two explicit sides mean two rails and the latch"));
	expectEqual(both.slotCount(false), 6, QStringLiteral("the 5.1 input side has six slots"));
	expectEqual(both.slotCount(true), 2, QStringLiteral("the Stereo output side has two slots"));
	expectEqual(QString::fromStdWString(both.slotRole(false, 3)), QStringLiteral("LFE"),
		QStringLiteral("slot roles follow the layout's channel order"));

	// Defaults mirror the engine: slot i reads the i-th selected channel,
	// slots beyond the selection are silent.
	VSTSlotFillModel narrow;
	narrow.setContract(VST3BusContract{VST3BusLayout::Surround51, VST3BusLayout::Surround51});
	narrow.setSelectedChannels({L"SL", L"SR"});
	expectTrue(narrow.sideDefaulted(false), QStringLiteral("no list means the side is defaulted"));
	expectEqual(QString::fromStdWString(narrow.slotValue(false, 0)), QStringLiteral("SL"),
		QStringLiteral("the default of slot 0 is the first selected channel"));
	expectEqual(QString::fromStdWString(narrow.slotValue(false, 2)), QStringLiteral("-"),
		QStringLiteral("slots beyond the selection default to silence"));
	expectTrue(narrow.slotSilent(false, 2), QStringLiteral("the beyond-selection default reads silent"));
	expectFalse(narrow.slotChannelMissing(false, 2),
		QStringLiteral("an implicit default never counts as missing"));

	// The first edit materializes the whole list from the effective values,
	// because a partial list is not expressible in the grammar.
	narrow.pickSlot(false, 1, L"SL");
	expectFalse(narrow.sideDefaulted(false), QStringLiteral("a pick materializes the list"));
	expectEqual(static_cast<int>(narrow.inputFill().size()), 6,
		QStringLiteral("the materialized list covers every slot"));
	expectEqual(QString::fromStdWString(narrow.inputFill()[0]), QStringLiteral("SL"),
		QStringLiteral("untouched slots keep their former effective value"));
	expectEqual(QString::fromStdWString(narrow.inputFill()[1]), QStringLiteral("SL"),
		QStringLiteral("the picked slot carries the picked channel"));
	expectEqual(QString::fromStdWString(narrow.inputFill()[5]), QStringLiteral("-"),
		QStringLiteral("beyond-selection slots materialize as silence"));

	// Out-of-selection detection uses the engine's resolver, so aliases keep
	// working (SL resolves onto a device that spells the pair RL/RR) and a
	// genuinely absent channel is flagged.
	VSTSlotFillModel missing;
	missing.setContract(VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Auto});
	missing.setSelectedChannels({L"L", L"R", L"RL", L"RR"});
	missing.pickSlot(false, 0, L"SL");
	expectFalse(missing.slotChannelMissing(false, 0),
		QStringLiteral("the SL alias resolves against a RL/RR selection"));
	missing.pickSlot(false, 0, L"TFL");
	expectTrue(missing.slotChannelMissing(false, 0),
		QStringLiteral("a channel outside the selection is flagged"));
	missing.pickSlot(false, 0, L"-");
	expectTrue(missing.slotSilent(false, 0) && !missing.slotChannelMissing(false, 0),
		QStringLiteral("a dash slot is silent, never missing"));

	// clearSide returns to the implicit default (the list leaves the line).
	missing.clearSide(false);
	expectTrue(missing.sideDefaulted(false) && missing.inputFill().empty(),
		QStringLiteral("clearing a side removes its list"));
}
