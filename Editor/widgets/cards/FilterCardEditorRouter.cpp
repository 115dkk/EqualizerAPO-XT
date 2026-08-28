/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Which card editor a "Filter:" line opens in.
*/

#include <string>

#include "AllPassCardEditor.h"
#include "FilterCardEditorRegistry.h"
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
// The order is not arbitrary. IIR is tried first because its parser is the
// stricter of the two and rejects everything that is not an explicit
// coefficient line. The all-pass is tried next, on the type the BiQuad parser
// reports. Anything else returns nullptr, which the factory chain reads as "no
// card editor" and answers with the legacy GUI - that nullptr is load-bearing,
// and it is the only thing keeping an ordinary "Filter: ON PK ..." out of the
// coefficient card.
REGISTER_FILTER_CARD_EDITOR(Filter, [](FilterTable*, const QString& command, const QString& parameters) -> IFilterGUI* {
	const std::wstring wideCommand = command.toStdWString();

	{
		IIRCommand cmd;
		std::wstring wideParameters = parameters.toStdWString();
		if (IIRFilterFactory::parseCommand(wideCommand, wideParameters, cmd))
			return new IIRCardEditor(cmd.order, cmd.coefficients);
	}

	{
		BiQuadCommand cmd;
		// parseCommand rewrites its parameters argument as it consumes tokens,
		// so each attempt needs its own copy of the original text.
		std::wstring wideParameters = parameters.toStdWString();
		if (BiQuadFilterFactory::parseCommand(wideCommand, wideParameters, cmd)
			&& (cmd.type == BiQuad::ALL_PASS || cmd.type == BiQuad::ALL_PASS_1))
		{
			// The command name is carried through verbatim so that editing
			// "Filter 99:" saves "Filter 99:" and not "Filter:".
			return new AllPassCardEditor(cmd, command);
		}
	}

	return nullptr;
})
