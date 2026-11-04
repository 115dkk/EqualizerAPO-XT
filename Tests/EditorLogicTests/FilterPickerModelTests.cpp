/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/FilterPickerModel.h"

TEST_CASE(EditorLogicTests, testFilterPickerModelMatchesTermsAndPreservesCatalogIndices)
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

TEST_CASE(EditorLogicTests, testFilterPickerTitlePartsTakeTheParentheticalApart)
{
	const FilterPickerTitleParts split = filterPickerTitleParts(
		QStringLiteral("Subwoofer routing (crossover + LFE routing)"));
	expectEqual(split.title, QStringLiteral("Subwoofer routing"), QStringLiteral("the name before the parenthetical is the title"));
	expectEqual(split.note, QStringLiteral("crossover + LFE routing"), QStringLiteral("the parenthetical is the note, parentheses dropped"));

	const FilterPickerTitleParts plain = filterPickerTitleParts(QStringLiteral("Peaking filter"));
	expectEqual(plain.title, QStringLiteral("Peaking filter"), QStringLiteral("a name without a parenthetical is the whole title"));
	expectTrue(plain.note.isEmpty(), QStringLiteral("a name without a parenthetical has no note"));

	// zh_CN writes the names with full-width parentheses and no space.
	const FilterPickerTitleParts wide = filterPickerTitleParts(
		QStringLiteral("Include") + QChar(0xFF08) + QStringLiteral("Include configuration file") + QChar(0xFF09));
	expectEqual(wide.title, QStringLiteral("Include"), QStringLiteral("full-width parentheses split like ASCII ones"));
	expectEqual(wide.note, QStringLiteral("Include configuration file"), QStringLiteral("the full-width note loses its parentheses too"));

	// Search still runs over the full name, so a description term finds the entry.
	const FilterPickerEntry entry{ { QStringLiteral("Control") },
		QStringLiteral("Include (Include configuration file)"), QStringLiteral("Include:"), QString() };
	expectTrue(filterPickerMatches(entry, QStringLiteral("Control"), QStringLiteral("configuration")),
		QStringLiteral("a term from the note still matches the entry"));
}

TEST_CASE(EditorLogicTests, testFilterPickerModelOwnsSelectionNavigation)
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
