/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <utility>

#include "diagnostics/performance/PerfProfile.h"
#include "services/logging/Logging.h"
#include "text/WideString.h"
#include "NewChannelCommand.h"
#include "NewChannelFilter.h"

using std::move;
using std::vector;
using std::wstring;

NewChannelFilter::NewChannelFilter(vector<wstring> names, vector<wstring> alreadyDeclared)
	: names(move(names)), alreadyDeclared(move(alreadyDeclared))
{
}

vector<wstring> NewChannelFilter::initialize(float sampleRate, unsigned maxFrameCount,
	vector<wstring> channelNames)
{
	vector<wstring> result = NewChannelCommand::extendSelection(move(channelNames), names);
	TraceF(L"Declared virtual channel(s) %s; selection is now %s",
		text::join(names, L", ").c_str(), text::join(result, L" ").c_str());
	if (!alreadyDeclared.empty())
		TraceF(L"Already declared, added to the selection only: %s",
			text::join(alreadyDeclared, L", ").c_str());
	return result;
}

#pragma AVRT_CODE_BEGIN
void NewChannelFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("NewChannelFilter::process");
	// nothing to do
}
#pragma AVRT_CODE_END
