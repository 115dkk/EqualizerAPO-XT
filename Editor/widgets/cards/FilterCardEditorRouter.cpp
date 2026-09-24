/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Which card editor a "Filter:" line opens in.
*/

#include <string>

#include "AllPassCardEditor.h"
#include "FilterCardEditorRegistry.h"
#include "FilterLineCard.h"
#include "IIRCardEditor.h"
#include "filters/BiQuadCommand.h"
#include "filters/BiQuadFilterFactory.h"
#include "filters/IIRCommand.h"
#include "filters/IIRFilterFactory.h"

// "Filter" is the one command keyword with more than one card behind it: IIR
// coefficients, an all-pass, and everything else that falls through to the
// legacy knob GUI. The registry keys on the keyword and holds one creator per
// key, so the choice between them has to be made inside a single registration.
//
// The alternative - registering "Filter" from each card's own translation unit
// and letting QHash::insert pick a winner - would depend on static
// initialization order across translation units, which is unspecified. It would
// appear to work, and then a link-order change would silently send every
// all-pass to the coefficient card.
//
// The choice is filterLineCard (FilterLineCard.h), registered as the entry's
// predicate so FilterCardEditorFactory::available answers per line: an
// ordinary "Filter: ON PK ..." is not a card line and takes the legacy knob
// GUI from the start (audit #348 TD-57). The creator constructs what the
// predicate chose; a nullptr from it still falls back to the legacy chain in
// FilterTable::createRowGui.
namespace
{
bool acceptsFilterLine(const QString& command, const QString& parameters)
{
	return filterLineCard(command, parameters) != FilterLineCard::None;
}

IFilterGUI* createFilterCard(FilterTable*, const QString& command, const QString& parameters)
{
	const std::wstring wideCommand = command.toStdWString();
	// parseCommand rewrites its parameters argument as it consumes tokens, so
	// each parse needs its own copy of the original text.
	std::wstring wideParameters = parameters.toStdWString();
	switch (filterLineCard(command, parameters))
	{
	case FilterLineCard::IirCoefficients:
	{
		IIRCommand cmd;
		if (IIRFilterFactory::parseCommand(wideCommand, wideParameters, cmd))
			return new IIRCardEditor(cmd.order, cmd.coefficients);
		break;
	}
	case FilterLineCard::AllPass:
	{
		BiQuadCommand cmd;
		// The command name is carried through verbatim so that editing
		// "Filter 99:" saves "Filter 99:" and not "Filter:".
		if (BiQuadFilterFactory::parseCommand(wideCommand, wideParameters, cmd))
			return new AllPassCardEditor(cmd, command);
		break;
	}
	case FilterLineCard::None:
		break;
	}
	return nullptr;
}
}

REGISTER_FILTER_CARD_EDITOR(Filter, createFilterCard, false, acceptsFilterLine)
