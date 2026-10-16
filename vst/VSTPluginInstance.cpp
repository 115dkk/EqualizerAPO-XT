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

short VSTPluginInstance::logicalEditorSize(int physical, double scaleFactor)
{
	return (short)std::max(1, (int)(physical / scaleFactor + 0.5));
}

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height, double scaleFactor)
{
	// Defaults are physical pixels; callers receive logical host-frame units.
	if (scaleFactor <= 0.0)
		scaleFactor = 1.0;
	if (width != nullptr)
		*width = logicalEditorSize(defaultEditorWidth, scaleFactor);
	if (height != nullptr)
		*height = logicalEditorSize(defaultEditorHeight, scaleFactor);
	return startEditingImpl(hWnd, width, height, scaleFactor);
}
