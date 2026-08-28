/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MultiConvolutionRoutingAdapter.h"

#include <algorithm>

using std::vector;

namespace
{
// A summand channel is an IR reference only when it is a plain decimal number;
// anything else can only come from a hand-edited line and is dropped on the
// way back to mappings.
bool parseIrIndex(const std::wstring& channel, unsigned& value)
{
	if (channel.empty() || channel.size() > 4)
		return false;
	for (wchar_t c : channel)
		if (c < L'0' || c > L'9')
			return false;
	value = (unsigned)wcstoul(channel.c_str(), nullptr, 10);
	return true;
}
}

vector<Assignment> MultiConvolutionRoutingAdapter::toAssignments(const vector<MultiConvolutionCommand::Mapping>& mappings,
	int fileChannelCount)
{
	vector<Assignment> assignments;
	assignments.reserve(mappings.size());

	for (const MultiConvolutionCommand::Mapping& mapping : mappings)
	{
		Assignment assignment;
		assignment.targetChannel = mapping.targetChannel;

		vector<MultiConvolutionCommand::IrChannelRef> irChannels = mapping.irChannels;
		if (irChannels.empty() && fileChannelCount > 0)
		{
			irChannels.resize((size_t)fileChannelCount);
			for (int c = 0; c < fileChannelCount; c++)
				irChannels[(size_t)c] = MultiConvolutionCommand::IrChannelRef((unsigned)c);
		}

		for (const MultiConvolutionCommand::IrChannelRef& ref : irChannels)
		{
			Assignment::Summand summand;
			summand.factor = ref.factor;
			summand.isDecibel = ref.isDecibel;
			summand.channel = std::to_wstring(ref.channel);
			assignment.sourceSum.push_back(summand);
		}

		assignments.push_back(assignment);
	}

	return assignments;
}

vector<MultiConvolutionCommand::Mapping> MultiConvolutionRoutingAdapter::toMappings(const vector<Assignment>& assignments)
{
	vector<MultiConvolutionCommand::Mapping> mappings;

	for (const Assignment& assignment : assignments)
	{
		if (assignment.targetChannel.empty())
			continue;

		MultiConvolutionCommand::Mapping mapping;
		mapping.targetChannel = assignment.targetChannel;
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			unsigned irChannel = 0;
			if (parseIrIndex(summand.channel, irChannel))
				mapping.irChannels.push_back(MultiConvolutionCommand::IrChannelRef(irChannel, summand.factor, summand.isDecibel));
		}

		// A row whose sum ends up empty is a seeded placeholder (or lost every
		// summand to hand-edited noise); skipping it mirrors the serializer.
		if (!mapping.irChannels.empty())
			mappings.push_back(mapping);
	}

	return mappings;
}

QStringList MultiConvolutionRoutingAdapter::sourcePorts(int fileChannelCount,
	const vector<MultiConvolutionCommand::Mapping>& mappings)
{
	QStringList ports;
	for (int c = 0; c < fileChannelCount; c++)
		ports.append(QString::number(c));

	// References beyond the file (or without a file) still get a port, sorted
	// numerically after the real channels, so the connection stays visible and
	// removable instead of silently disappearing from the view.
	vector<unsigned> extra;
	for (const MultiConvolutionCommand::Mapping& mapping : mappings)
		for (const MultiConvolutionCommand::IrChannelRef& ref : mapping.irChannels)
			if ((int)ref.channel >= fileChannelCount)
				extra.push_back(ref.channel);
	std::sort(extra.begin(), extra.end());
	extra.erase(std::unique(extra.begin(), extra.end()), extra.end());
	for (unsigned c : extra)
		ports.append(QString::number(c));

	return ports;
}
