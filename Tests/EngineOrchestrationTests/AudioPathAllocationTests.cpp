/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The running engine must not ask the heap for memory from its process call.
	The executable-wide allocation replacements below are armed only around the
	measured calls and count only the calling thread.
*/

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <malloc.h>
#include <new>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Tests/WavFixtures.h"
#include "EngineOrchestrationTestSupport.h"

#ifndef __SANITIZE_ADDRESS__
namespace allocation_gate
{
	thread_local bool armed = false;
	thread_local size_t allocations = 0;

	void* allocate(size_t size)
	{
		// Adapted from Custom Audio Manager by weirdsoup (MIT); see ThirdPartyNotices.txt.
		if (armed)
			allocations++;
		if (void* memory = std::malloc(size != 0 ? size : 1))
			return memory;
		throw std::bad_alloc();
	}

	void* allocateAligned(size_t size, size_t alignment)
	{
		if (armed)
			allocations++;
		if (void* memory = _aligned_malloc(size != 0 ? size : 1, alignment))
			return memory;
		throw std::bad_alloc();
	}

	void begin() noexcept
	{
		allocations = 0;
		armed = true;
	}

	size_t end() noexcept
	{
		armed = false;
		return allocations;
	}
}

void* operator new(size_t size)
{
	return allocation_gate::allocate(size);
}

void* operator new[](size_t size)
{
	return allocation_gate::allocate(size);
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete[](void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, size_t) noexcept
{
	std::free(memory);
}

void operator delete[](void* memory, size_t) noexcept
{
	std::free(memory);
}

void* operator new(size_t size, std::align_val_t alignment)
{
	return allocation_gate::allocateAligned(size, static_cast<size_t>(alignment));
}

void* operator new[](size_t size, std::align_val_t alignment)
{
	return allocation_gate::allocateAligned(size, static_cast<size_t>(alignment));
}

void operator delete(void* memory, std::align_val_t) noexcept
{
	_aligned_free(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
	_aligned_free(memory);
}

void operator delete(void* memory, size_t, std::align_val_t) noexcept
{
	_aligned_free(memory);
}

void operator delete[](void* memory, size_t, std::align_val_t) noexcept
{
	_aligned_free(memory);
}
#endif

namespace
{
std::wstring executableDirectory()
{
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();
	std::wstring result(path, length);
	const size_t separator = result.find_last_of(L"\\/");
	return separator == std::wstring::npos ? std::wstring() : result.substr(0, separator);
}

std::wstring writeAllocationIr(test::Harness& harness)
{
	const std::wstring path = testDirectory() + L"\\allocation-gate-ir.wav";
	std::vector<double> samples(32, 0.0);
	samples[0] = 1.0;
	trackWrittenFile(path);
	harness.require(test::writeWavFile(path, 48000, 1, samples), "the allocation-gate impulse response is written");
	return path;
}
}

TEST_CASE(EngineOrchestrationTests, testFilterEngineProcessDoesNotAllocate)
{
#ifdef __SANITIZE_ADDRESS__
	std::printf("FilterEngine allocation contract: skipped under AddressSanitizer because its allocator interposition replaces the measured heap boundary\n");
#else
	const std::wstring irPath = writeAllocationIr(harness);
	const std::wstring vstPath = executableDirectory() + L"\\TestVst2Plugin.dll";
	harness.require(GetFileAttributesW(vstPath.c_str()) != INVALID_FILE_ATTRIBUTES,
		"TestVst2Plugin.dll is present for the audio-path allocation gate");
	const std::string configText =
		"Filter: ON PK Fc 1000 Hz Gain 3 dB Q 0.707\n"
		"GraphicEQ: 20 0; 1000 -1; 20000 0\n"
		"Convolution: \"" + toNarrow(irPath) + "\"\n"
		"Delay: 1 ms\n"
		"Copy: L=0.75*L+0.25*R R=R\n"
		"VSTPlugin: Library \"" + toNarrow(vstPath) + "\" Gain 0.5\n";
	const std::wstring config = writeConfig(harness, L"allocation-gate.txt", configText);

	constexpr unsigned frames = 256;
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, frames, config);
	std::vector<float> input(static_cast<size_t>(frames) * 2, 0.25f);
	std::vector<float> output(static_cast<size_t>(frames) * 2, 0.0f);
	for (unsigned block = 0; block < 12; block++)
		engine.process(output.data(), input.data(), frames);

	allocation_gate::begin();
	for (unsigned block = 0; block < 16; block++)
		engine.process(output.data(), input.data(), frames);
	const size_t allocations = allocation_gate::end();

	harness.expectEqual(allocations, static_cast<size_t>(0),
		"FilterEngine::process allocated from the heap after warm-up; keep this gate failing and report the allocating audio-path site");
#endif
}
