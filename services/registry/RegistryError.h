/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#pragma once

#include <string>

class RegistryError
{
public:
	explicit RegistryError(const std::wstring& message)
		: message(message)
	{
	}

	const std::wstring& getMessage() const
	{
		return message;
	}

private:
	std::wstring message;
};
