/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "devices/HostAutoStart.h"

#include "asio/AsioRegistration.h"
#include "asio/HostLaunch.h"
#include "asio/WrapperRecord.h"
#include "devices/ReceiverEndpoints.h"

void refreshHostAutoStart(IRegistry& registry, const std::wstring& installDirectory)
{
	const bool wanted = eapo::asio::WrapperRecords::autoStartWanted(registry) || !receivingEndpoints(registry).empty();
	if (wanted && installDirectory.empty())
		return;
	eapo::asio::AsioRegistration::setAutoStart(registry, eapo::asio::HostLaunch::executablePath(installDirectory), wanted);
}
