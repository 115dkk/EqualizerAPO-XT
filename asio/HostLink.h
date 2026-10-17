/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The port between the daemon adapter and whatever runs the engine host:
	a real process reached through the control pipe (Win32HostLink), or a
	thread inside the calling process for the tests and the probe
	(ThreadHostLink). Both keep their mapped region and kernel objects private
	and hand back a RingProducer only after the host reaches Ready.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "asio/StreamProcessor.h"
#include "runtime/ipc/StreamRing.h"

namespace eapo::asio
{
	struct HostSession
	{
		void* ringBase = nullptr;
		size_t ringBytes = 0;
		eapo::ipc::RingSync sync;       // work/done/ready plus peer = the host
		uint32_t hostPid = 0;
	};

	// Completes the common half of both links. The peer handle must already be
	// present in session.sync: no producer is exposed before the host answers.
	inline std::unique_ptr<eapo::ipc::RingProducer> readyProducer(HostSession& session, const StreamFormat& format,
		uint32_t readyTimeoutMs, OpenReport& report)
	{
		auto producer = std::make_unique<eapo::ipc::RingProducer>(session.ringBase, format, GetCurrentProcessId(), session.sync);
		const eapo::ipc::RingState state = producer->waitReady(readyTimeoutMs);
		if (state == eapo::ipc::RingState::Ready)
			return producer;
		if (state == eapo::ipc::RingState::Fault)
		{
			report.status = producer->fault() == eapo::ipc::RingFault::LayoutMismatch
				? OpenReport::Status::Rejected : OpenReport::Status::Unavailable;
			copyErrorMessage(report.message, producer->fault() == eapo::ipc::RingFault::LayoutMismatch
				? "EQ APO XT engine host does not understand this wrapper's stream layout"
				: "EQ APO XT engine host could not load the configuration for this stream");
		}
		else
		{
			report.status = OpenReport::Status::Unavailable;
			copyErrorMessage(report.message, "EQ APO XT engine host did not become ready in time");
		}
		producer->close();
		return nullptr;
	}

	class IHostLink
	{
	public:
		virtual ~IHostLink() = default;

		// Creates the region and its objects, reaches (or starts) the host, and
		// returns a producer that has passed the Ready barrier. Failure is mapped
		// to report, including RingFault. The link keeps the backing objects.
		virtual std::unique_ptr<eapo::ipc::RingProducer> open(const StreamFormat& format, const StreamOptions& options,
			OpenReport& report) = 0;

		// Closes the producer before releasing its mapped region and handles. The
		// producer is passed by value so callers cannot reverse that order.
		virtual void close(std::unique_ptr<eapo::ipc::RingProducer> producer) noexcept = 0;
	};
}
