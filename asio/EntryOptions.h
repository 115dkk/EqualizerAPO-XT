/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The choices the Device Selector offers for one ASIO entry, whichever
	kind of target stands behind it: an ASIO driver row (AsioAPOInfo) and an
	endpoint's "Use in ASIO apps" entry (DeviceAPOInfo) show the same four
	controls and write them into the same wrapper record fields
	(WrapperRecords::entryOptions and setEntryOptions). Kept apart from
	WrapperRecord.h so DeviceAPOInfo's install state can hold it without
	pulling in the stream vocabulary.
*/

#pragma once

#include "asio/AsioConstants.h"

namespace eapo::asio
{
	struct EntryOptions
	{
		// No extra buffer: the host is waited for inside the buffer switch,
		// and a missed deadline passes the buffer through unprocessed.
		bool synchronous = false;
		// How much of the buffer period a synchronous switch waits: 25, 50 or 75.
		unsigned deadlinePercent = defaultDeadlinePercent;
		// Start the engine host at boot (one Run value for the machine).
		bool autoStart = false;
		// Register the entry for 32-bit hosts too, when the x86 wrapper ships.
		bool host32 = false;

		bool operator==(const EntryOptions&) const = default;
	};
}
