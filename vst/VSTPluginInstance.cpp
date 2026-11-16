/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include "stdafx.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VST3SpeakerMapping.h"
#include "dsp/SampleConversion.h"
#include <cctype>
#include <limits>

// These factories live in separate translation units: the VST2 and VST3
// SDKs have conflicting VST_VERSION definitions.
std::unique_ptr<VSTPluginInstance> createVST2Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
std::unique_ptr<VSTPluginInstance> createVST3Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);

std::unique_ptr<VSTPluginInstance> VSTPluginInstance::create(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
{
	return library->isVST3() ? createVST3Instance(library, processLevel) : createVST2Instance(library, processLevel);
}

bool VSTPluginInstance::isVST3() const
{
	return library->isVST3();
}

namespace
{
// A plugin that declares itself an up/downmixer or spatializer processes a
// stereo source into the speaker layout; its input bus must stay stereo
// while only the output bus spans the device. The OpenSpatial Upmixer
// accepts a symmetric multichannel layout but leaves its engine disengaged
// there, which is why the declared role - not the accepted layout - drives
// the choice (probe evidence in PR #213).
bool isUpmixerSubCategory(const std::string& subCategories)
{
	std::string lower = subCategories;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower.find("up-downmix") != std::string::npos
		|| lower.find("updownmix") != std::string::npos
		|| lower.find("spatial") != std::string::npos
		|| lower.find("surround") != std::string::npos;
}

template<class Sample>
bool allocateConversion(int channels, int frames, std::vector<Sample*>& pointers,
	AlignedMemory::UniqueAllocation<Sample>& buffer)
{
	// Plugin dimensions must not overflow before widening; the old engine
	// allocation made the same check (CodeQL cpp/integer-multiplication-cast-to-long).
	if (static_cast<size_t>(channels) > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(frames))
		return false;
	pointers.resize(channels);
	if (channels == 0)
		return true;
	buffer = AlignedMemory::allocateArray<Sample>(static_cast<size_t>(channels) * frames);
	if (!buffer)
		return false;
	for (int channel = 0; channel < channels; channel++)
		pointers[channel] = buffer.get() + static_cast<size_t>(channel) * frames;
	return true;
}
}

bool VSTPluginInstance::metadataValid() const
{
	// Plugins can report absurd counts or latency. testRejectedNegotiationMetadata
	// uses TestVst2Plugin's huge/negative modes; audit #467 TD-21.
	return numInputs() >= 0 && numInputs() <= 1024
		&& numOutputs() >= 0 && numOutputs() <= 1024
		&& getInitialDelay() >= 0 && getInitialDelay() <= 16 * 1024 * 1024;
}

VSTPluginInstance::NegotiationResult VSTPluginInstance::negotiate(const NegotiationRequest& request)
{
	NegotiationResult result;
	if (!metadataValid())
	{
		result.reason = L"Invalid channel or latency metadata";
		return result;
	}
	if (request.contract && isVST3())
	{
		result.path = NegotiationResult::Path::Contract;
		const auto& contract = *request.contract;
		result.accepted = negotiateBusLayouts(contract.input, contract.output, request.channelCount,
			vst3speakers::channelNamesForLayout(contract.input, request.channelNames),
			vst3speakers::channelNamesForLayout(contract.output, request.channelNames));
		if (!result.accepted)
			result.reason = L"Requested bus contract was refused";
	}
	else
	{
		const bool fullWidth = request.channelCount > 0 && request.channelCount <= 1024
			&& negotiateChannelCount(request.channelCount, request.channelNames);
		result.path = fullWidth ? NegotiationResult::Path::Automatic : NegotiationResult::Path::Preferred;
		if (request.channelCount > 2 && request.channelCount <= 1024
			&& (request.stereoInput || isUpmixerSubCategory(library->getVST3SubCategories()))
			&& negotiateBusChannelCounts(2, request.channelCount, {L"L", L"R"}, request.channelNames))
			result.path = NegotiationResult::Path::StereoInput;
		result.accepted = true;
	}
	if (!metadataValid())
	{
		result.accepted = false;
		result.reason = L"Invalid channel or latency metadata";
	}
	if (result.accepted)
	{
		result.inputs = numInputs();
		result.outputs = numOutputs();
		result.inputLayout = getNegotiatedVST3InputLayout();
		result.outputLayout = getNegotiatedVST3OutputLayout();
	}
	return result;
}

bool VSTPluginInstance::prepareForProcessing(float rate, int blockSize, InputWidth width,
	const std::shared_ptr<ProcessingWorkspace>& sharedWorkspace)
{
	processingFrames = 0;
	if (blockSize <= 0 || !metadataValid())
		return false;
	processingInputs = numInputs();
	processingOutputs = numOutputs();
	if (!prepareProcessingImpl(rate, blockSize) || !metadataValid()
		|| numInputs() != processingInputs || numOutputs() != processingOutputs)
		return false;
	workspace = sharedWorkspace ? sharedWorkspace : std::make_shared<ProcessingWorkspace>();
	const bool needsDouble = width == InputWidth::Float && canDoubleReplacing();
	if (workspace->inputs != processingInputs || workspace->outputs != processingOutputs
		|| workspace->frames != blockSize)
	{
		workspace->frames = 0;
		if (!allocateConversion(processingInputs, blockSize, workspace->floatInputs, workspace->floatInputBuffer)
			|| !allocateConversion(processingOutputs, blockSize, workspace->floatOutputs, workspace->floatOutputBuffer))
			return false;
		workspace->inputs = processingInputs;
		workspace->outputs = processingOutputs;
		workspace->frames = blockSize;
	}
	if (needsDouble
		&& (!allocateConversion(processingInputs, blockSize, workspace->doubleInputs, workspace->doubleInputBuffer)
			|| !allocateConversion(processingOutputs, blockSize, workspace->doubleOutputs, workspace->doubleOutputBuffer)))
		return false;
	processingFrames = static_cast<unsigned>(blockSize);
	return true;
}

void VSTPluginInstance::processBlock(double** input, double** output, unsigned frames)
{
	if (frames > processingFrames || !workspace)
		return;
	if (canDoubleReplacing())
		processDoubleReplacing(input, output, static_cast<int>(frames));
	else
	{
		for (int channel = 0; channel < processingInputs; channel++)
			sampleconv::demote(workspace->floatInputs[channel], input[channel], frames);
		processBlock(workspace->floatInputs.data(), workspace->floatOutputs.data(), frames);
		for (int channel = 0; channel < processingOutputs; channel++)
			sampleconv::promote(output[channel], workspace->floatOutputs[channel], frames);
	}
}

void VSTPluginInstance::processBlock(float** input, float** output, unsigned frames)
{
	if (frames > processingFrames || !workspace)
		return;
	if (canDoubleReplacing())
	{
		for (int channel = 0; channel < processingInputs; channel++)
			sampleconv::promote(workspace->doubleInputs[channel], input[channel], frames);
		processDoubleReplacing(workspace->doubleInputs.data(), workspace->doubleOutputs.data(), static_cast<int>(frames));
		for (int channel = 0; channel < processingOutputs; channel++)
			sampleconv::demote(output[channel], workspace->doubleOutputs[channel], frames);
	}
	else if (canReplacing())
		processReplacing(input, output, static_cast<int>(frames));
	else
	{
		for (int channel = 0; channel < processingOutputs; channel++)
			std::fill_n(output[channel], frames, 0.0f);
		process(input, output, static_cast<int>(frames));
	}
}

bool VSTPluginInstance::ProcessingLease::acquire(VSTPluginInstance* instance)
{
	if (owner == instance && owner != nullptr)
		return true;
	reset();
	if (instance == nullptr)
		return false;
	if (instance->canProcessNow())
		return true;
	if (!instance->startProcessing())
		return false;
	owner = instance;
	return true;
}

void VSTPluginInstance::ProcessingLease::reset()
{
	if (owner != nullptr)
		owner->stopProcessingSafely();
	owner = nullptr;
}

short VSTPluginInstance::logicalEditorSize(int physical, double scaleFactor)
{
	return (short)std::max(1, (int)(physical / scaleFactor + 0.5));
}

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height, double scaleFactor)
{
	// Defaults are physical pixels; callers receive logical host-frame units.
	if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0)
		scaleFactor = 1.0;
	if (width != nullptr)
		*width = logicalEditorSize(defaultEditorWidth, scaleFactor);
	if (height != nullptr)
		*height = logicalEditorSize(defaultEditorHeight, scaleFactor);
	return startEditingImpl(hWnd, width, height, scaleFactor);
}

bool VSTPluginInstance::setEditorScale(double scaleFactor)
{
	if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0)
		scaleFactor = 1.0;
	return rescaleEditorImpl(scaleFactor);
}
