/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TitleBarSkin.h"

namespace
{
TitleBarSkin* installedSkin = nullptr;
}

TitleBarSkin* TitleBarSkin::current()
{
	return installedSkin;
}

void TitleBarSkin::install(TitleBarSkin* skin)
{
	installedSkin = skin;
}
