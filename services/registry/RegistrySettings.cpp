/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "IRegistry.h"
#include "RegistryError.h"
#include "RegistryPaths.h"
#include "RegistrySettings.h"

bool readParallelConvolution(const IRegistry& registry) noexcept
{
	try
	{
		return registry.readValue(APP_REGPATH, parallelConvolutionValueName) != L"false";
	}
	catch (const RegistryError&)
	{
		return true;
	}
}

bool readIgnoreConvolutionSilence(const IRegistry& registry) noexcept
{
	try
	{
		return registry.readValue(APP_REGPATH, ignoreConvolutionSilenceValueName) != L"false";
	}
	catch (const RegistryError&)
	{
		return true;
	}
}
