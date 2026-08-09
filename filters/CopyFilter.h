/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/IFilter.h"

struct Assignment
{
	std::wstring targetChannel;

	struct Summand
	{
		double factor = 0.0;
		bool isDecibel = false;
		std::wstring channel;
	};

	std::vector<Summand> sourceSum;
};

// Shared, Qt-free parser for a "Copy:" config line. It turns the parameter
// string into the same std::vector<Assignment> that CopyFilter::getAssignments()
// returns: assignments are split on spaces, each "target=source" is split on
// '=', the source is split into '+' summands, every summand is split on '*' into
// an optional factor and a channel, a lone token is treated as a factor only
// when it is "0" or contains a '.', and the dB suffix sets isDecibel. The
// engine (CopyFilterFactory) and the Editor GUI factory share this one routine,
// so the grammar cannot diverge between them.
std::vector<Assignment> parseCopyAssignments(const std::wstring& parameters);

// Re-creates the canonical "target=source ..." parameter string for a set of
// assignments. This is the single owner of the Copy serialization format that
// the Editor's CopyFilterGUI::store() and CopyRoutingAdapter::serialize() emit,
// so serializeCopyAssignments(parseCopyAssignments(line)) round-trips. Summands
// with a single-space channel (the GUI's "not yet filled row" sentinel) and
// assignments with an empty target are skipped.
std::wstring serializeCopyAssignments(const std::vector<Assignment>& assignments);

// Applies Copy's channel-flow semantics without constructing an Editor GUI.
// Every assignment that has at least one real summand makes its target
// available to the commands below it. Existing names and aliases are kept in
// their canonical spelling through ChannelLayout::getChannelIndex().
void propagateCopyChannels(const std::vector<Assignment>& assignments,
	std::vector<std::wstring>& channelNames);

#pragma AVRT_VTABLES_BEGIN
class CopyFilter : public IFilter
{
public:
	CopyFilter(const std::vector<Assignment>& assignments);
	~CopyFilter() override = default;
	bool getAllChannels() override {return true;}
	bool getInPlace() override {return false;}
	bool producesTailFromSilentInput() const override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	const std::vector<Assignment>& getAssignments() const;

private:
	std::vector<Assignment> assignments;

	struct InternalSummand
	{
		int channel;
		double factor;

		InternalSummand(int channel, double factor) noexcept
			: channel(channel), factor(factor)
		{
		}
	};

	struct InternalAssignment
	{
		int targetChannel;
		std::vector<InternalSummand> sourceSum;

		InternalAssignment(int targetChannel, std::vector<InternalSummand> sourceSum)
			: targetChannel(targetChannel), sourceSum(std::move(sourceSum))
		{
		}
	};

	std::vector<InternalAssignment> internalAssignments;
	size_t inputChannelCount = 0;
	bool hasNonzeroConstant = false;
};
#pragma AVRT_VTABLES_END
