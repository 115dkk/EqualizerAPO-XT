/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/InProcProcessor.h"

#include <cstring>
#include <exception>

#include "engine/FilterEngine.h"

namespace eapo::asio
{
	namespace
	{
		void copyMessage(char (&destination)[124], const char* source) noexcept
		{
			size_t i = 0;
			for (; i + 1 < sizeof(destination) && source[i] != '\0'; i++)
				destination[i] = source[i];
			destination[i] = '\0';
		}
	}

	InProcProcessor::InProcProcessor() = default;

	InProcProcessor::~InProcProcessor()
	{
		close(StreamStats());
	}

	OpenReport InProcProcessor::open(const StreamFormat& format, const StreamOptions& options)
	{
		OpenReport report;
		close(StreamStats());

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
				EngineSetup setup;
				setup.sampleRate = static_cast<float>(format.sampleRate);
				setup.inputChannelCount = channels;
				setup.realChannelCount = channels;
				setup.outputChannelCount = channels;
				setup.channelMask = 0;
				setup.maxFrameCount = format.frames;
				setup.customPath = options.configPath;
				setup.preMix = false;
				setup.capture = direction == Direction::Input;
				setup.postMixInstalled = true;
				setup.deviceName = format.deviceName;
				setup.connectionName = L"ASIO";
				setup.deviceGuid = format.deviceGuid;
				lane.engine->initialize(setup);
			}
			catch (const std::exception& e)
			{
				close(StreamStats());
				report.status = OpenReport::Status::Rejected;
				copyMessage(report.message, e.what());
				return report;
			}
			catch (...)
			{
				close(StreamStats());
				report.status = OpenReport::Status::Rejected;
				copyMessage(report.message, "the in-process engine could not be initialized");
				return report;
			}
			report.planes[slot] = lane.planes.data();
		}

		report.status = OpenReport::Status::Ok;
		report.extraLatencyFrames = 0;
		return report;
	}

	Outcome InProcProcessor::process(Direction direction) noexcept
	{
		Lane& lane = lanes_[static_cast<unsigned>(direction)];
		if (lane.engine == nullptr)
			return Outcome::Off;
		lane.engine->process(lane.planes.data(), lane.planes.data(), lane.frames);
		return Outcome::Processed;
	}

	void InProcProcessor::close(const StreamStats& stats) noexcept
	{
		lastStats_ = stats;
		for (Lane& lane : lanes_)
		{
			lane.engine.reset();
			lane.planes.clear();
			lane.storage.clear();
			lane.frames = 0;
		}
	}
}
