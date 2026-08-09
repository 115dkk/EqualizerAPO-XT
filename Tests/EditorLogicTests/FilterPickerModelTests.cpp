/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/FilterPickerModel.h"

void testFilterPickerModelMatchesTermsAndPreservesCatalogIndices()
{
	QList<FilterPickerEntry> entries;
	entries.append({ { QStringLiteral("Basic") }, QStringLiteral("Gain"), QStringLiteral("Preamp: 0 dB"), QString() });
	entries.append({ { QStringLiteral("Phase & Time") }, QStringLiteral("Delay"), QStringLiteral("Delay: 0 ms"), QString() });
	entries.append({ { QStringLiteral("Phase & Time") }, QStringLiteral("All-pass"), QStringLiteral("Filter: ON AP Fc 1000 Hz"), QString() });

	FilterPickerModel model;
	model.setEntries(entries);
	model.setQuery(QStringLiteral("phase delay"));
	const QList<FilterPickerMatch> matches = model.matches();

	expectEqual(matches.size(), 1, QStringLiteral("all search terms must match one entry haystack"));
	expectEqual(matches.first().originalIndex, 1, QStringLiteral("a filtered row keeps its catalog index"));
	expectEqual(matches.first().section, QStringLiteral("Phase & Time"), QStringLiteral("the match carries its section"));
}

void testFilterPickerModelOwnsSelectionNavigation()
{
	QList<FilterPickerEntry> entries;
	entries.append({ { QStringLiteral("Basic") }, QStringLiteral("Gain"), QStringLiteral("Preamp: 0 dB"), QString() });
	entries.append({ { QStringLiteral("Phase & Time") }, QStringLiteral("Delay"), QStringLiteral("Delay: 0 ms"), QString() });
	entries.append({ { QStringLiteral("Phase & Time") }, QStringLiteral("All-pass"), QStringLiteral("Filter: ON AP Fc 1000 Hz"), QString() });

	FilterPickerModel model;
	model.setEntries(entries);
	model.setQuery(QStringLiteral("phase"));
	expectEqual(model.selectedIndex(), 1, QStringLiteral("a query selects its first matching catalog entry"));

	model.moveSelection(1);
	expectEqual(model.selectedIndex(), 2, QStringLiteral("down moves through matching catalog entries"));
	model.moveSelection(1);
	expectEqual(model.selectedIndex(), 2, QStringLiteral("selection clamps at the final matching entry"));
	expectTrue(model.selectIndex(1), QStringLiteral("a visible catalog entry can be selected explicitly"));
	expectEqual(model.selectedIndex(), 1, QStringLiteral("explicit selection stores the catalog index"));

	model.setQuery(QStringLiteral("gain"));
	expectEqual(model.selectedIndex(), 0, QStringLiteral("a new query resets selection to its first match"));
	expectFalse(model.selectIndex(2), QStringLiteral("a filtered-out catalog entry cannot become current"));
	expectEqual(model.selectedIndex(), 0, QStringLiteral("a rejected selection leaves the current entry intact"));
}
