/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/VSTBusModel.h"

void testVSTBusModelMigratesAndEdits()
{
	// A bare line: no contract, both directions read Auto.
	VSTBusModel bare;
	expectFalse(bare.contract().has_value(), QStringLiteral("no contract on a bare line"));
	expectTrue(bare.input() == VST3BusLayout::Auto && bare.output() == VST3BusLayout::Auto,
		QStringLiteral("a bare line reads Auto/Auto"));

	// Legacy StereoInput migrates once to the equivalent asymmetric contract.
	VSTBusModel legacy(std::nullopt, true);
	expectTrue(legacy.contract().has_value(), QStringLiteral("legacy StereoInput becomes a contract"));
	expectTrue(legacy.input() == VST3BusLayout::Stereo && legacy.output() == VST3BusLayout::Auto,
		QStringLiteral("the migrated contract is Input Stereo, Output Auto"));
	expectTrue(legacy.migratedLegacyStereoInput(), QStringLiteral("the migration is flagged for the card"));

	// A hand-edited line carrying both generations: the explicit pair wins.
	VSTBusModel both(VST3BusContract{VST3BusLayout::Surround51, VST3BusLayout::Surround71}, true);
	expectTrue(both.input() == VST3BusLayout::Surround51 && both.output() == VST3BusLayout::Surround71,
		QStringLiteral("an explicit pair is authoritative over the legacy flag"));
	expectFalse(both.migratedLegacyStereoInput(),
		QStringLiteral("nothing was migrated when the pair was explicit"));

	// Editing replaces the contract and clears the migration flag.
	legacy.setLayouts(VST3BusLayout::Surround714, VST3BusLayout::Surround714);
	expectTrue(legacy.input() == VST3BusLayout::Surround714,
		QStringLiteral("an edit stores the picked layouts"));
	expectFalse(legacy.migratedLegacyStereoInput(),
		QStringLiteral("an edit ends the migration note"));

	// Removal returns the line to bare Auto negotiation.
	legacy.clear();
	expectFalse(legacy.contract().has_value(), QStringLiteral("clear removes the contract"));
	expectTrue(legacy.input() == VST3BusLayout::Auto && legacy.output() == VST3BusLayout::Auto,
		QStringLiteral("a cleared model reads Auto/Auto again"));
}
