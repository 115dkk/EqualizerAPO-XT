/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "filters/SendFilterFactory.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio/ChannelLayout.h"
#include "engine/FilterEngine.h"
#include "filters/FilterFactoryRegistry.h"
#include "filters/SendCommand.h"
#include "filters/SendFilter.h"
#include "platform/windows/TextEncoding.h"
#include "runtime/ipc/SendRing.h"
#include "services/logging/Logging.h"
#include "text/WideString.h"

// cppcheck-suppress unknownMacro ; cppcheck does not expand this variadic self-registration macro, which MSVC does
REGISTER_FILTER_FACTORY(FilterFactoryPriority::Send, SendFilterFactory, L"Send")

namespace
{
	using eapo::ipc::send::MixMode;
	using eapo::ipc::send::SendParams;

	std::wstring senderText(uint64_t senderId)
	{
		wchar_t buffer[32] = {};
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%016llx",
			static_cast<unsigned long long>(senderId));
		return buffer;
	}

}

void SendFilterFactory::initialize(FilterEngine* engine)
{
	ParseReportingFactory::initialize(engine);
	if (engine_ != nullptr)
		engine_->setInputTap(nullptr);
	receiverInstalled_ = false;
	receiver_.reset();
	engine_ = engine;
	ownGuid_.clear();
	if (engine_ == nullptr)
		return;

	engine_->setInputTap(nullptr);
	ownGuid_ = SendCommand::canonicalEndpoint(engine_->getDeviceGuid());
	if (engine_->getHost() == EngineHost::Apo && !engine_->isCapture() && !engine_->isPreMix()
		&& !ownGuid_.empty())
	{
		// An exception here would leave FilterEngine::initialize and fail the
		// APO's LockForProcess; repeated failures make Windows turn off every
		// system effect on the endpoint. Receiving is optional, so log and go on.
		try
		{
			receiver_.configure(engine_->getSendNamePrefix(), ownGuid_, engine_->getSampleRate(),
				engine_->getMaxFrameCount(), engine_->deviceChannelNames());
			receiverInstalled_ = true;
			engine_->setInputTap(&receiver_);
		}
		catch (const std::exception& e)
		{
			receiver_.reset();
			LogF(L"Receiving Send audio is unavailable: %s",
				wintext::toWideString(e.what(), CP_UTF8).c_str());
		}
	}
	else if (engine_->getHost() == EngineHost::Apo && !engine_->isCapture() && engine_->isPreMix()
		&& !engine_->isPostMixInstalled() && !missingPostMixLogged_)
	{
		// A trace, not a log line: every stream on a pre-mix-only endpoint
		// passes here, and most of those endpoints never receive anything.
		TraceF(L"Receiving Send audio needs the post-mix stage installed on this endpoint");
		missingPostMixLogged_ = true;
	}
}

FilterVector SendFilterFactory::startOfConfiguration()
{
	for (auto& entry : writers_)
		entry.second->markUnused();
	targetsInLoad_.clear();
	compensationDelays_.clear();

	if (engine_ != nullptr && receiverInstalled_)
	{
		engine_->watchEvent(receiver_.readyEventName());
		receiver_.tryAttach();
		reportReceiverNews(receiver_.takeNews());
	}
	return {};
}

void SendFilterFactory::reportReceiverNews(const SendReceiverNews& news)
{
	if (news.attached)
	{
		LogF(L"Receiving %u channel(s) from sender %s into %s",
			news.channelCount, senderText(news.senderId).c_str(),
			text::join(news.targetNames, L", ").c_str());
	}
	if (!news.ignoredNames.empty())
		LogF(L"Ignoring Send target channel(s) this endpoint does not have: %s",
			text::join(news.ignoredNames, L", ").c_str());
	if (!news.refusal.empty())
		LogF(L"Not receiving Send audio: %s", news.refusal.c_str());
	if (news.senderLeft)
		LogF(L"Sender for this endpoint left");
	if (news.underrunsChanged)
		LogF(L"Send underruns: %llu (consider a larger Latency)",
			static_cast<unsigned long long>(news.underruns));
}

FilterVector SendFilterFactory::createFilter(const std::wstring& configPath,
	std::wstring& command, std::wstring& parameters)
{
	if (command != L"Send")
		return {};

	SendCommand parsed;
	std::wstring error;
	if (!SendCommand::parse(command, parameters, parsed, &error))
		return reportParseError(command, error);
	if (engine_ == nullptr)
		return {};
	if (engine_->getHost() != EngineHost::Apo)
	{
		TraceF(L"Send is inactive outside the audio service");
		// Processing factories must leave command set when they produce no filter.
		return {};
	}
	if (engine_->isCapture())
		return reportParseError(command, L"Send is not available on recording endpoints");
	if (engine_->isPreMix())
		return reportParseError(command, L"Send requires the post-mix stage to be installed on this endpoint");
	if (parsed.endpoint == ownGuid_)
		return reportParseError(command, L"Send cannot target its own endpoint");
	if (targetsInLoad_.count(parsed.endpoint) != 0)
	{
		return reportParseError(command,
			L"Another Send line in this configuration already feeds " + parsed.endpoint);
	}

	double delayFrameValue = 0.0;
	if (parsed.latencyUnit == SendCommand::LatencyUnit::Samples)
		delayFrameValue = parsed.latency;
	else if (parsed.latencyUnit == SendCommand::LatencyUnit::Milliseconds)
		delayFrameValue = std::round(parsed.latency * engine_->getSampleRate() / 1000.0);
	else
		delayFrameValue = 2.0 * engine_->getMaxFrameCount();
	const uint32_t reserve = 2 * engine_->getMaxFrameCount();
	const double maximumDelay = reserve >= eapo::ipc::send::capacityFrames ? 0.0
		: static_cast<double>(eapo::ipc::send::capacityFrames - reserve);
	if (delayFrameValue >= maximumDelay)
	{
		std::wostringstream reason;
		reason << L"Latency of " << std::fixed << std::setprecision(0) << delayFrameValue
			<< L" frames is too large for the Send ring";
		return reportParseError(command, reason.str());
	}
	const uint32_t delayFrames = static_cast<uint32_t>(delayFrameValue);

	std::vector<Assignment> assignments;
	assignments.reserve(parsed.assignments.size());
	for (Assignment assignment : parsed.assignments)
	{
		bool valid = true;
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			if (ChannelLayout::getChannelIndex(summand.channel,
				engine_->loadingChannelNames(), true) == -1)
			{
				reportParseError(command, L"Send source channel " + summand.channel
					+ L" does not exist here");
				valid = false;
			}
		}
		if (valid)
			assignments.push_back(std::move(assignment));
	}
	if (assignments.empty())
		return {};

	auto writerIt = writers_.find(parsed.endpoint);
	if (writerIt == writers_.end())
	{
		// A failure to open the ring refuses this line only; letting it escape
		// would discard the whole configuration being loaded.
		std::unique_ptr<SendWriter> writer;
		try
		{
			writer = std::make_unique<SendWriter>(engine_->getSendNamePrefix(), parsed.endpoint);
		}
		catch (const std::exception& e)
		{
			return reportParseError(command, L"Could not open the Send ring for " + parsed.endpoint
				+ L": " + wintext::toWideString(e.what(), CP_UTF8));
		}
		writerIt = writers_.emplace(parsed.endpoint, std::move(writer)).first;
	}
	SendWriter* const writer = writerIt->second.get();
	if (writer->otherSenderLive())
		return reportParseError(command, L"Another sender already feeds " + parsed.endpoint);
	targetsInLoad_.insert(parsed.endpoint);

	SendParams params;
	params.senderId = writer->senderId();
	params.sampleRate = engine_->getSampleRate();
	params.delayFrames = delayFrames;
	params.senderMaxFrameCount = engine_->getMaxFrameCount();
	params.mixMode = parsed.mode == SendCommand::Mode::Replace ? MixMode::Replace : MixMode::Mix;
	for (const Assignment& assignment : assignments)
		params.targetNames.push_back(assignment.targetChannel);
	writer->publishIfChanged(params);
	writer->markUsed();
	if (parsed.compensate)
		compensationDelays_.push_back(delayFrames);

	TraceF(L"Send: %s %u channel(s), delay %u frames (%.1f ms), compensating %u output channel(s)",
		parsed.endpoint.c_str(), static_cast<unsigned>(assignments.size()), delayFrames,
		1000.0 * delayFrames / engine_->getSampleRate(),
		parsed.compensate ? engine_->getOutputChannelCount() : 0);
	return singleFilter(makeFilter<SendFilter>(engine_, writer, assignments));
}

FilterVector SendFilterFactory::endOfConfiguration()
{
	for (auto& entry : writers_)
	{
		if (!entry.second->usedInLoad())
			entry.second->close();
	}
	if (compensationDelays_.empty() || engine_ == nullptr)
		return {};
	const uint32_t maxDelay = *std::max_element(
		compensationDelays_.cbegin(), compensationDelays_.cend());
	return singleFilter(makeFilter<SendCompensationFilter>(
		maxDelay, engine_->getOutputChannelCount()));
}
