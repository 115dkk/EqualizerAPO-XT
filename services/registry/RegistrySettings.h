/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

class IRegistry;

// Defaults to enabled when the value is absent or cannot be read. Only the
// exact REG_SZ value L"false" disables trailing-silence removal.
bool readIgnoreConvolutionSilence(const IRegistry& registry) noexcept;

// Defaults to enabled; only the exact REG_SZ value L"false" disables it.
bool readParallelConvolution(const IRegistry& registry) noexcept;
