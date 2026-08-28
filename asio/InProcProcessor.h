/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The in-process adapter at the processor seam: one FilterEngine per
	direction, linked into the calling process. It is the measurement
	baseline and the CI comparison for the daemon adapter, and it lives only
	in the probe and the tests: the shipped wrapper DLL never links the
	engine (docs/architecture/asio-host-study.md, section 10).
*/

#pragma once

#include <memory>
#include <vector>

#include "asio/StreamProcessor.h"

class FilterEngine;

namespace eapo::asio
{
	class InProcProcessor final : public IStreamProcessor
	{
	public:
		InProcProcessor();
		~InProcProcessor() override;

		InProcProcessor(const InProcProcessor&) = delete;
		InProcProcessor& operator=(const InProcProcessor&) = delete;

		OpenReport open(const StreamFormat& format, const StreamOptions& options) override;
		Outcome process(Direction direction) noexcept override;
		void close(const StreamStats& stats) noexcept override;

		// What the last close() received; the probe reads it.
		const StreamStats& lastStats() const noexcept {return lastStats_;}

	private:
		struct Lane
		{
			std::unique_ptr<FilterEngine> engine;
			std::vector<float> storage;
			std::vector<float*> planes;
			unsigned frames = 0;
		};

		Lane lanes_[directionCount];
		StreamStats lastStats_;
	};
}
