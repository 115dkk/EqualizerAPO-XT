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
#include <vector>

#include "IFilter.h"

struct Assignment
{
	std::wstring targetChannel;

	struct Summand
	{
		double factor;
		bool isDecibel;
		std::wstring channel;
	};

	std::vector<Summand> sourceSum;
};

// Shared, Qt-free parser for a "Copy:" config line. It turns the parameter
// string into the same std::vector<Assignment> that CopyFilter::getAssignments()
// returns, reproducing CopyFilterFactory::createFilter's former inline parse
// verbatim: assignments are split on spaces, each "target=source" is split on
// '=', the source is split into '+' summands, every summand is split on '*' into
// an optional factor and a channel, a lone token is treated as a factor only
// when it is "0" or contains a '.', and the dB suffix sets isDecibel. The
// resulting assignments build the identical CopyFilter, so copy_crossfeed stays
// bit-identical. The engine (CopyFilterFactory) and the Editor GUI factory share
// this one routine instead of each carrying their own copy of the grammar.
std::vector<Assignment> parseCopyAssignments(const std::wstring& parameters);

// Re-creates the canonical "target=source ..." parameter string for a set of
// assignments. This is the single owner of the Copy serialization format that
// the Editor's CopyFilterGUI::store() and CopyRoutingAdapter::serialize() emit,
// so serializeCopyAssignments(parseCopyAssignments(line)) round-trips. Summands
// with a single-space channel (the GUI's "not yet filled row" sentinel) and
// assignments with an empty target are skipped, matching the GUI's behaviour.
std::wstring serializeCopyAssignments(const std::vector<Assignment>& assignments);

#pragma AVRT_VTABLES_BEGIN
class CopyFilter : public IFilter
{
public:
	CopyFilter(const std::vector<Assignment>& assignments);
	virtual ~CopyFilter();
	bool getAllChannels() override {return true;}
	bool getInPlace() override {return false;}
	bool producesTailFromSilentInput() const override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	std::vector<Assignment> getAssignments() const;

private:
	void cleanup();

	std::vector<Assignment> assignments;

	struct InternalAssignment
	{
		int targetChannel;

		struct InternalSummand
		{
			double factor;
			int channel;
		};

		InternalSummand* sourceSum;
		unsigned sourceCount;
	};

	InternalAssignment* internalAssignments;
	unsigned assignmentCount;
};
#pragma AVRT_VTABLES_END
