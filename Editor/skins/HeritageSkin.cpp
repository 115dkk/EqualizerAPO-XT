/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "HeritageSkin.h"

#include "SkinThemeData.h"

QString HeritageSkin::id() const
{
	return QStringLiteral("heritage");
}

SkinTokens HeritageSkin::tokens(bool dark) const
{
	Q_UNUSED(dark);
	// Classic light values for the custom painters that consume tokens; the
	// palette is shared with Device Selector's heritage theme.
	return SkinThemeData::heritageTokens();
}

IRoutingRenderer* HeritageSkin::routingRenderer() const
{
	return nullptr;
}

void HeritageSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	// Native toolbar: the .ui's classic icons stay in place.
	Q_UNUSED(toolBar);
	Q_UNUSED(tokens);
}

void HeritageSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	// The dialog stays platform-native in heritage mode.
	Q_UNUSED(dialog);
	Q_UNUSED(tokens);
}

ISkin* heritageSkin()
{
	static HeritageSkin instance;
	return &instance;
}
