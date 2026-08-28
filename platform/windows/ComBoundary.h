/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <new>
#include <utility>
#include <winerror.h>

class ComBoundary
{
public:
	template <typename Function>
	static HRESULT invoke(Function&& function)
	{
		try
		{
			return std::forward<Function>(function)();
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
	}
};
