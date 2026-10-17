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
		float** planes(Direction direction) noexcept override;
		Outcome process(Direction direction) noexcept override;
		void close() noexcept override;

	private:
		struct Lane
		{
			std::unique_ptr<FilterEngine> engine;
			std::vector<float> storage;
			std::vector<float*> planes;
			unsigned frames = 0;
		};

		Lane lanes_[directionCount];
	};
}
