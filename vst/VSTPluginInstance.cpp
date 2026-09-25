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

// VSTPluginInstance is the facade its callers use; the work happens in one of
// two implementations of VSTFormatInstance, chosen here once from the loaded
// library's ABI (audit #348 C4/TD-40):
//   - VST2Instance.cpp             : the AEffect, its host callback, loading
//                                    with its __try/__except guard, process,
//                                    editor and state.
//   - VST3Instance*.cpp            : the VST3 component/controller pair; see
//                                    VST3Instance.h for its translation units.
// Every method below forwards; none of them asks which format it hosts.

#include "stdafx.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTFormatInstance.h"

using namespace std;

VSTPluginInstance::VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
	: library(library),
	host(library->isVST3() ? createVST3Instance(library, processLevel) : createVST2Instance(library, processLevel))
{
}

VSTPluginInstance::~VSTPluginInstance()
{
	// A plug-in may still call back while it is torn down; it must not reach
	// a caller's closure then.
	host->setAutomateFunc(nullptr);
	host->setSizeWindowFunc(nullptr);
	host.reset();
}

bool VSTPluginInstance::initialize()
{
	return host->initialize();
}

int VSTPluginInstance::numInputs() const
{
	return host->numInputs();
}

int VSTPluginInstance::numOutputs() const
{
	return host->numOutputs();
}

bool VSTPluginInstance::negotiateChannelCount(int channelCount,
	const vector<wstring>& channelNames)
{
	return host->negotiateChannelCount(channelCount, channelNames);
}

bool VSTPluginInstance::negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
	const vector<wstring>& inputChannelNames, const vector<wstring>& outputChannelNames)
{
	return host->negotiateBusChannelCounts(inputChannelCount, outputChannelCount,
		inputChannelNames, outputChannelNames);
}

bool VSTPluginInstance::negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
	int automaticChannelCount, const vector<wstring>& inputChannelNames,
	const vector<wstring>& outputChannelNames)
{
	return host->negotiateBusLayouts(inputLayout, outputLayout, automaticChannelCount,
		inputChannelNames, outputChannelNames);
}

std::optional<VST3BusLayout> VSTPluginInstance::getNegotiatedVST3InputLayout() const
{
	return host->negotiatedInputLayout();
}

std::optional<VST3BusLayout> VSTPluginInstance::getNegotiatedVST3OutputLayout() const
{
	return host->negotiatedOutputLayout();
}

const std::vector<int>& VSTPluginInstance::getVST3InputChannelMapping() const
{
	return host->inputChannelMapping();
}

const std::vector<int>& VSTPluginInstance::getVST3OutputChannelMapping() const
{
	return host->outputChannelMapping();
}

bool VSTPluginInstance::canReplacing() const
{
	return host->canReplacing();
}

bool VSTPluginInstance::isVST3() const
{
	return library->isVST3();
}

bool VSTPluginInstance::canProcessNow() const
{
	return host->canProcessNow();
}

int VSTPluginInstance::uniqueID() const
{
	return host->uniqueID();
}

std::wstring VSTPluginInstance::getName() const
{
	return host->getName();
}

int VSTPluginInstance::getUsedChannelCount() const
{
	return host->getUsedChannelCount();
}

void VSTPluginInstance::setUsedChannelCount(int count)
{
	host->setUsedChannelCount(count);
}

float VSTPluginInstance::getSampleRate() const
{
	return host->getSampleRate();
}

int VSTPluginInstance::getProcessLevel() const
{
	return host->getProcessLevel();
}

void VSTPluginInstance::setProcessLevel(int value)
{
	host->setProcessLevel(value);
}

int VSTPluginInstance::getLanguage() const
{
	return host->getLanguage();
}

void VSTPluginInstance::setLanguage(int value)
{
	host->setLanguage(value);
}

bool VSTPluginInstance::canDoubleReplacing() const
{
	return host->canDoubleReplacing();
}

int VSTPluginInstance::getInitialDelay() const
{
	return host->getInitialDelay();
}

void VSTPluginInstance::prepareForProcessing(float sampleRate, int blockSize)
{
	host->prepareForProcessing(sampleRate, blockSize);
}

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	host->writeToEffect(chunkData, paramMap);
}

void VSTPluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
	host->readFromEffect(chunkData, paramMap);
}

void VSTPluginInstance::startProcessing()
{
	host->startProcessing();
}

void VSTPluginInstance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
	host->processDoubleReplacing(inputArray, outputArray, frameCount);
}

void VSTPluginInstance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	host->processReplacing(inputArray, outputArray, frameCount);
}

void VSTPluginInstance::process(float** inputArray, float** outputArray, int frameCount)
{
	host->process(inputArray, outputArray, frameCount);
}

void VSTPluginInstance::stopProcessing()
{
	host->stopProcessing();
}

void VSTPluginInstance::stopProcessingSafely() noexcept
{
	host->stopProcessingSafely();
}

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height, double scaleFactor)
{
	// scaleFactor is the host frame's device pixel ratio. Until the plug-in
	// reports its own size, the caller gets a 400x300 physical-pixel default
	// in its logical units.
	if (scaleFactor <= 0.0)
		scaleFactor = 1.0;
	const auto toLogical = [scaleFactor](int physical) -> short {
		return (short)max(1, (int)(physical / scaleFactor + 0.5));
	};
	if (width != NULL)
		*width = toLogical(400);
	if (height != NULL)
		*height = toLogical(300);
	return host->startEditing(hWnd, width, height, scaleFactor);
}

void VSTPluginInstance::doIdle()
{
	host->doIdle();
}

void VSTPluginInstance::stopEditing()
{
	host->stopEditing();
}

void VSTPluginInstance::setAutomateFunc(std::function<void()> func)
{
	host->setAutomateFunc(std::move(func));
}

void VSTPluginInstance::onAutomate()
{
	host->onAutomate();
}

void VSTPluginInstance::setSizeWindowFunc(std::function<void(int, int)> func)
{
	host->setSizeWindowFunc(std::move(func));
}

void VSTPluginInstance::onSizeWindow(int w, int h)
{
	host->onSizeWindow(w, h);
}
