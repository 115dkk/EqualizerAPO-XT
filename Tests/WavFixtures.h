/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Shared WAV fixture writer for the test suites (audit #275 D5/TD-23): the
	libsndfile boilerplate for writing a double-precision test WAV existed
	five times across the suites, in mono and multi-channel variants. One
	writer, both shapes. Returns false instead of asserting so each suite
	keeps its own failure policy (harness.fail vs require).

	Requires libsndfile (sndfile.h) and the RAII handle from
	audio/io/SndfileRAII.h, which every consumer already links.
*/

#pragma once

#include <string>
#include <vector>

#include <sndfile.h>

#include "audio/io/SndfileRAII.h"

namespace test
{

// Writes interleaved double samples as a WAV (SF_FORMAT_DOUBLE). frames must
// equal interleaved.size() / channelCount.
inline bool writeWavFile(const std::wstring& path, int sampleRate, unsigned channelCount,
	const std::vector<double>& interleaved)
{
	if (channelCount == 0 || interleaved.size() % channelCount != 0)
		return false;
	SF_INFO info = {};
	info.samplerate = sampleRate;
	info.channels = static_cast<int>(channelCount);
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;

	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_WRITE, &info));
	if (!file)
		return false;
	const sf_count_t frames = static_cast<sf_count_t>(interleaved.size() / channelCount);
	return sf_writef_double(file.get(), interleaved.data(), frames) == frames;
}

// Mono convenience overload.
inline bool writeWavFile(const std::wstring& path, int sampleRate, const std::vector<double>& samples)
{
	return writeWavFile(path, sampleRate, 1, samples);
}

// Per-channel convenience overload; every channel must have the same length.
inline bool writeWavFile(const std::wstring& path, int sampleRate,
	const std::vector<std::vector<double>>& channels)
{
	if (channels.empty())
		return false;
	const size_t frames = channels[0].size();
	for (const std::vector<double>& channel : channels)
	{
		if (channel.size() != frames)
			return false;
	}
	std::vector<double> interleaved(frames * channels.size());
	for (size_t frame = 0; frame < frames; frame++)
		for (size_t c = 0; c < channels.size(); c++)
			interleaved[frame * channels.size() + c] = channels[c][frame];
	return writeWavFile(path, sampleRate, static_cast<unsigned>(channels.size()), interleaved);
}

} // namespace test
