/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

class IRegistry;

// Child APO subkey names, as stored, with receiving enabled and a Render key.
// Only the exact string "false" disables an existing REG_SZ value. Unreadable
// records are logged and skipped; a missing Child APOs tree is empty.
std::vector<std::wstring> receivingEndpoints(const IRegistry& registry);
