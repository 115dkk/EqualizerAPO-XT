/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

class IRegistry;

// Recomputes the Run value from wrapper records and Send receivers:
// present while an enabled wrapper asks for it or a receiver exists.
// Needs the install directory only to write the value; with none
// known, a wanted value that is already there stays as it is.
void refreshHostAutoStart(IRegistry& registry, const std::wstring& installDirectory);
