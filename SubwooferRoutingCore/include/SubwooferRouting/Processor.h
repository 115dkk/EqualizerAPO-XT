// SPDX-License-Identifier: MIT

#pragma once

#include "Graph.h"

#include <cstddef>
#include <memory>

namespace subroute
{

enum class SampleFormat
{
	Float32,
	Float64
};

class AudioBlock
{
public:
	AudioBlock(
		const float* const* inputs,
		float* const* outputs,
		std::size_t channelCount,
		std::size_t frameCount) noexcept;

	AudioBlock(
		const double* const* inputs,
		double* const* outputs,
		std::size_t channelCount,
		std::size_t frameCount) noexcept;

	SampleFormat sampleFormat() const noexcept;
	std::size_t channelCount() const noexcept;
	std::size_t frameCount() const noexcept;

	const float* const* floatInputs() const noexcept;
	float* const* floatOutputs() const noexcept;

	const double* const* doubleInputs() const noexcept;
	double* const* doubleOutputs() const noexcept;

private:
	SampleFormat sampleFormat_;
	std::size_t channelCount_;
	std::size_t frameCount_;
	const float* const* floatInputs_;
	float* const* floatOutputs_;
	const double* const* doubleInputs_;
	double* const* doubleOutputs_;
};

class Processor
{
public:
	Processor();
	~Processor();

	Processor(const Processor&) = delete;
	Processor& operator=(const Processor&) = delete;

	Processor(Processor&& other) noexcept;
	Processor& operator=(Processor&& other) noexcept;

	/*
		Allocates and initializes all float and double scratch buffers, delay
		lines, biquad states, path buffers, and original-input preservation
		buffers needed by process(). The graph must have been compiled for an
		equivalent PrepareSpec.

		prepare() may allocate and may throw std::invalid_argument,
		std::length_error, or std::bad_alloc. It must not run on the audio
		thread.
	*/
	void prepare(
		const PrepareSpec& prepareSpec,
		const ProcessingGraph& graph);

	/*
		Clears all delay lines, filter histories, and scratch state without
		allocating, locking, performing I/O, or throwing.
	*/
	void reset() noexcept;

	/*
		After a successful prepare(), process() performs no heap allocation,
		locking, I/O, or exception propagation.

		A valid block has:
		- the same channel count as PrepareSpec::channelLayout;
		- no more than PrepareSpec::maximumBlockSize frames;
		- a non-null selected input-plane array and output-plane array;
		- a non-null input and output plane for every channel;
		- distinct output planes, although each output plane may alias its
		  corresponding or another input plane.

		The processor first preserves every original physical input plane, so
		all source mixes read original input even for in-place processing.
		Untouched output channels are copied directly in the block's native
		sample type and are therefore bit-exact.

		If the processor is unprepared or the block violates the structural
		contract above, process() performs no work.
	*/
	void process(const AudioBlock& block) noexcept;

	bool isPrepared() const noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

}
