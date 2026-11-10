/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

// Whether two VSTPlugin ChunkData strings (base64) hold the same plug-in state
// once the times inside ZIP records are set aside. Some plug-ins (Kilohearts)
// write their state as a ZIP archive anew on every read, so only its file
// times change; compared byte for byte, an embedded panel marked the document
// unsaved every second or two with nothing touched (#459). Accepted risk: a
// real change confined to the four time/date bytes after a byte sequence that
// merely looks like a ZIP header goes unnoticed.
bool vstChunksEquivalent(const std::wstring& first, const std::wstring& second);
