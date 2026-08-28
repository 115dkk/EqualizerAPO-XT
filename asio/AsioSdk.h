/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The one include for the Steinberg ASIO SDK headers. The SDK is fetched at
	build time (deps/asiosdk, pinned by SHA-256 in .github/simd-variants.psd1;
	GPLv3 dual-licensed since 2.3.4) and is never vendored. Every ASIO-facing
	translation unit includes this header instead of the SDK files directly so
	the include order (asiosys.h before asio.h before iasiodrv.h) and the
	warning policy live in one place.
*/

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// iasiodrv.h spells its vtable with the COM `interface` keyword and derives
// from IUnknown; lean-and-mean windows.h leaves both to objbase.h.
#include <objbase.h>

#pragma warning(push)
// The SDK headers predate /W4 hygiene: unreferenced parameters and
// nameless struct members are theirs, not ours.
#pragma warning(disable: 4100 4201)
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"
#pragma warning(pop)
