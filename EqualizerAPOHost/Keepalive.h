/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <thread>

#include "asio/HostControlServer.h"
#include "services/registry/WindowsRegistry.h"

class IRegistry;

// Every host starts this supervisor after winning its single-instance mutex.
// Endpoint workers count as host activity; destruction stops and joins them.
// Device Selector starts the host now and the HKLM Run entry starts it at logon,
// not at boot: before logon there is no host and no Send keepalive. Every COM
// object stays on its MTA thread.
std::jthread startKeepalive(eapo::asio::HostActivity& activity, const IRegistry& registry = systemRegistry());
