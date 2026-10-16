/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/InProcProcessor.h"

#include <cstring>
#include <exception>

#include "asio/StreamEngineSetup.h"
#include "engine/FilterEngine.h"

namespace eapo::asio
{
	InProcProcessor::InProcProcessor() = default;

	InProcProcessor::~InProcProcessor()
	{
		close();
	}

	OpenReport InProcProcessor::open(const StreamFormat& format, const StreamOptions& options)
	{
		OpenReport report;
		close();

		for (unsigned slot = 0; slot < directionCount; slot++)
		{
			const Direction direction = static_cast<Direction>(slot);
			const unsigned channels = format.channelCount(direction);
			if (channels == 0)
				continue;

			Lane& lane = lanes_[slot];
			try
			{
				lane.frames = format.frames;
				lane.storage.assign(static_cast<size_t>(channels) * format.frames, 0.0f);
				lane.planes.resize(channels);
				for (unsigned c = 0; c < channels; c++)
					lane.planes[c] = lane.storage.data() + static_cast<size_t>(c) * format.frames;

				lane.engine = std::make_unique<FilterEngine>();
				lane.engine->initialize(streamEngineSetup(format, direction, options.configPath));
			}
			catch (const std::exception& e)
			{
				close();
				report.status = OpenReport::Status::Rejected;
				copyErrorMessage(report.message, e.what());
				return report;
			}
			catch (...)
			{
				close();
				report.status = OpenReport::Status::Rejected;
				copyErrorMessage(report.message, "the in-process engine could not be initialized");
				return report;
			}
		}

		report.status = OpenReport::Status::Ok;
		report.extraLatencyFrames = 0;
		return report;
	}

	float** InProcProcessor::planes(Direction direction) noexcept
	{
		Lane& lane = lanes_[slotOf(direction)];
		return lane.engine != nullptr ? lane.planes.data() : nullptr;
	}

	Outcome InProcProcessor::process(Direction direction) noexcept
	{
		Lane& lane = lanes_[slotOf(direction)];
		if (lane.engine == nullptr)
			return Outcome::Off;
		lane.engine->process(lane.planes.data(), lane.planes.data(), lane.frames);
		return Outcome::Processed;
	}

	void InProcProcessor::close() noexcept
	{
		for (Lane& lane : lanes_)
		{
			lane.engine.reset();
			lane.planes.clear();
			lane.storage.clear();
			lane.frames = 0;
		}
	}
}
