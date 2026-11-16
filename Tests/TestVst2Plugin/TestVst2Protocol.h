/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>

namespace testvst2
{
// Chunk blob layout: magic + version + the two float parameters, all
// little-endian. The host stores this base64-encoded, so its exact bytes must
// be stable for the round-trip assertion.
const uint32_t kChunkMagic = 0x32505654u; // 'TVP2' little-endian ('T','V','P','2')
const uint32_t kChunkVersion = 1u;

#pragma pack(push, 1)
struct ChunkBlob
{
	uint32_t magic;
	uint32_t version;
	float gain;
	float bypass;
};
#pragma pack(pop)
}
