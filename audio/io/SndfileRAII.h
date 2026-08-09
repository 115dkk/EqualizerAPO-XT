/*
	This file is part of Equalizer APO, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#ifndef ENABLE_SNDFILE_WINDOWS_PROTOTYPES
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif
#include <sndfile.h>

#include <memory>

namespace sndfile
{
	struct CloseDeleter
	{
		void operator()(SNDFILE* file) const noexcept
		{
			if (file != nullptr)
				sf_close(file);
		}
	};

	using Handle = std::unique_ptr<SNDFILE, CloseDeleter>;
}
