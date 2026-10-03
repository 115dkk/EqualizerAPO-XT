/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <thread>

// Only the resident host, after winning its single-instance mutex, starts this
// supervisor. Destruction requests stop and joins all endpoint workers. The
// HKLM Run entry starts at user logon, not at boot: before logon there is no
// resident host and no Send keepalive. Every COM object stays on its MTA thread.
std::jthread startKeepalive();
