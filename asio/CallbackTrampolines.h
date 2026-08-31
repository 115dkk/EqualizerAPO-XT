/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ASIOCallbacks carries no context pointer: a target driver calls plain
	function pointers. To route a target's callbacks to one wrapper instance,
	each instance claims one of a small pool of statically generated callback
	sets for the life of its buffers. Four slots is more than any host needs
	(a DAW opens one device), and enough for the probe to hold several
	wrappers side by side.
*/

#pragma once

#include "asio/AsioSdk.h"

namespace eapo::asio
{
	// What a wrapper receives from its target driver. All four run on
	// whatever thread the target chooses; the sink must not assume any.
	class ITargetCallbackSink
	{
	public:
		virtual ~ITargetCallbackSink() = default;
		virtual void onBufferSwitch(long doubleBufferIndex, ASIOBool directProcess) noexcept = 0;
		virtual ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) noexcept = 0;
		virtual void onSampleRateDidChange(ASIOSampleRate rate) noexcept = 0;
		virtual long onAsioMessage(long selector, long value, void* message, double* opt) noexcept = 0;
	};

	namespace CallbackTrampolines
	{
		constexpr unsigned slotCount = 4;

		// Binds a free or fully drained retired slot to the sink and returns
		// its callback set, or nullptr when every slot is taken. Control thread only.
		ASIOCallbacks* claim(ITargetCallbackSink* sink) noexcept;

		// Closes and unbinds the sink's slot without waiting. Returns true when
		// no callback still holds the slot lease. Control thread only.
		bool release(const ITargetCallbackSink* sink) noexcept;
		bool drained(const ASIOCallbacks* callbacks) noexcept;

		// Test seam for simulating an entrant stalled before the sink load.
		bool testEnter(const ASIOCallbacks* callbacks) noexcept;
		void testLeave(const ASIOCallbacks* callbacks) noexcept;
	}
}
