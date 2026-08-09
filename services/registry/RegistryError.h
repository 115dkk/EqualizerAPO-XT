/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
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
