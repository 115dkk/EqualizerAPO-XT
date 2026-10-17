/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "runtime/errors/WideError.h"

class ComError : public WideError
{
public:
	using WideError::WideError;
};
