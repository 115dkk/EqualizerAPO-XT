/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Audit #348 TD-57: FilterCardEditorFactory::available answered for the
	keyword alone, so every "Filter: ON PK" line was decided as a card and
	FilterTable undid the decision. The Filter entry now registers a
	predicate, filterLineCard, and available() consults it.
*/

#include "EditorLogicTestSupport.h"

#include <QString>

#include "Editor/widgets/cards/FilterCardEditorFactory.h"
#include "Editor/widgets/cards/FilterCardEditorRegistry.h"
#include "Editor/widgets/cards/FilterLineCard.h"

namespace
{
bool acceptsFilterLine(const QString& command, const QString& parameters)
{
	return filterLineCard(command, parameters) != FilterLineCard::None;
}

IFilterGUI* noCard(FilterTable*, const QString&, const QString&)
{
	return nullptr;
}
}

void testFilterLineCardDecidesPerLine()
{
	expectTrue(filterLineCard(QStringLiteral("Filter"), QStringLiteral("ON PK Fc 1000 Hz Gain 3 dB Q 1")) == FilterLineCard::None,
		QStringLiteral("a peaking biquad opens no card"));
	expectTrue(filterLineCard(QStringLiteral("Filter"), QStringLiteral("ON LSC Fc 100 Hz Gain 4 dB")) == FilterLineCard::None,
		QStringLiteral("a shelf opens no card"));
	expectTrue(filterLineCard(QStringLiteral("Filter"), QStringLiteral("ON AP Fc 100 Hz Q 0.707")) == FilterLineCard::AllPass,
		QStringLiteral("an all-pass opens the all-pass card"));
	expectTrue(filterLineCard(QStringLiteral("Filter 2"), QStringLiteral("ON IIR Order 1 Coefficients 1 0 0.5 0")) == FilterLineCard::IirCoefficients,
		QStringLiteral("a numbered IIR coefficient line opens the coefficient card"));
	expectTrue(filterLineCard(QStringLiteral("Filter"), QStringLiteral("garbage")) == FilterLineCard::None,
		QStringLiteral("an unparsable line opens no card"));

	// The factory's answer goes through the registry predicate. This binary
	// has no card editors linked in, so the test registers the Filter entry
	// the way FilterCardEditorRouter.cpp does, with a creator that builds
	// nothing.
	FilterCardEditorRegistry::registerEditor(QStringLiteral("Filter"), noCard, false, acceptsFilterLine);
	expectFalse(FilterCardEditorFactory::available(QStringLiteral("Filter"), QStringLiteral("ON PK Fc 1000 Hz Gain 3 dB Q 1")),
		QStringLiteral("available() says no card for a peaking line"));
	expectTrue(FilterCardEditorFactory::available(QStringLiteral("Filter 1"), QStringLiteral("ON AP Fc 100 Hz Q 0.707")),
		QStringLiteral("available() says card for an all-pass line under a numbered key"));
	expectTrue(FilterCardEditorRegistry::accepts(QStringLiteral("Filter"), QStringLiteral("Filter"),
		QStringLiteral("ON IIR Order 1 Coefficients 1 0 0.5 0")), QStringLiteral("the registry predicate accepts an IIR line"));
	expectFalse(FilterCardEditorRegistry::accepts(QStringLiteral("NoSuchKeyword"), QStringLiteral("NoSuchKeyword"), QString()),
		QStringLiteral("a keyword without an entry is not accepted"));
}
