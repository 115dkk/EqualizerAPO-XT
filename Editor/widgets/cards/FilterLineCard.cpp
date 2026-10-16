/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FilterLineCard.h"

#include <string>

#include <QString>

#include "filters/BiQuadCommand.h"
#include "filters/IIRCommand.h"

FilterLineCard filterLineCard(const QString& command, const QString& parameters)
{
	const std::wstring wideCommand = command.toStdWString();

	{
		IIRCommand cmd;
		std::wstring wideParameters = parameters.toStdWString();
		if (IIRCommand::parse(wideCommand, wideParameters, cmd))
			return FilterLineCard::IirCoefficients;
	}

	{
		BiQuadCommand cmd;
		// The codec's parse rewrites its parameters argument as it consumes tokens,
		// so each attempt needs its own copy of the original text.
		std::wstring wideParameters = parameters.toStdWString();
		if (BiQuadCommand::parse(wideCommand, wideParameters, cmd)
			&& (cmd.type == BiQuad::ALL_PASS || cmd.type == BiQuad::ALL_PASS_1))
			return FilterLineCard::AllPass;
	}

	return FilterLineCard::None;
}
