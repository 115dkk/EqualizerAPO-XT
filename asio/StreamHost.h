/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The product operation behind HostControlServer's open handler. It owns the
	mapped ring and its serving thread for every accepted stream.
*/

#pragma once

#include <memory>
#include <vector>

#include "asio/HostProtocol.h"

class IRegistry;

namespace eapo::asio
{
	class HostActivity;

	class StreamHost
	{
	public:
		StreamHost(HostActivity& activity, IRegistry& registry);
		~StreamHost();

		StreamHost(const StreamHost&) = delete;
		StreamHost& operator=(const StreamHost&) = delete;

		HostOpenReply open(HostOpenRequest request);
		void stop() noexcept;

	private:
		struct ServeThread;

		void serve(HostOpenRequest request, ServeThread& worker) noexcept;
		void reap();

		HostActivity& activity_;
		IRegistry& registry_;
		std::vector<std::unique_ptr<ServeThread>> workers_;
	};
}
