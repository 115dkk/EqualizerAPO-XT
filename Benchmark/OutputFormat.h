/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>
#include <string_view>

// The sample format of the file Benchmark writes. 16-bit PCM stays the default
// because the panel input gate reads it. The engine hands Benchmark 32-bit
// float samples, so float32 writes them unchanged: a rendered impulse response
// can be loaded as a filter again without losing anything. pcm24 sits between
// the two for tools that cannot read float files.
enum class BenchmarkOutputFormat
{
	Pcm16,
	Pcm24,
	Float32,
};

constexpr std::optional<BenchmarkOutputFormat> parseBenchmarkOutputFormat(std::string_view name)
{
	if (name == "pcm16")
		return BenchmarkOutputFormat::Pcm16;
	if (name == "pcm24")
		return BenchmarkOutputFormat::Pcm24;
	if (name == "float32")
		return BenchmarkOutputFormat::Float32;
	return std::nullopt;
}

constexpr bool benchmarkOutputFormatIsInteger(BenchmarkOutputFormat format)
{
	return format != BenchmarkOutputFormat::Float32;
}
