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
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

static BOOL CALLBACK showChildWindow(HWND hWnd, LPARAM)
{
	ShowWindow(hWnd, SW_SHOW);
	return TRUE;
}

static const wchar_t* vst3EditorHostWindowClass = L"EqualizerAPOVST3EditorHost";

static void registerVST3EditorHostWindowClass()
{
	static bool registered = false;
	if (registered)
		return;

	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = vst3EditorHostWindowClass;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassW(&wc);
	registered = true;
}

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height, double scaleFactor)
{
	// VST3 ViewRect sizes are in physical pixels, but the Qt frame that hosts the
	// plugin lives in logical (device-independent) pixels. Keep them apart: the
	// native host window is sized in physical px, while *width/*height report
	// logical px so the caller's QWidget geometry is correct on a high-DPI
	// monitor. scaleFactor is the host frame's device pixel ratio; 1.0 (a 100%
	// display) makes every conversion below a no-op, so nothing changes there.
	if (scaleFactor <= 0.0)
		scaleFactor = 1.0;
	editorScaleFactor = scaleFactor;

	auto toLogical = [scaleFactor](int physical) -> short {
		return (short)max(1, (int)(physical / scaleFactor + 0.5));
	};

	int physWidth = 400;
	int physHeight = 300;
	if (width != NULL)
		*width = toLogical(physWidth);
	if (height != NULL)
		*height = toLogical(physHeight);

	if (library->isVST3())
	{
		if (vst3Controller == NULL)
			return false;
		stopEditing();
		vst3View = IPtr<IPlugView>::adopt(vst3Controller->createView(ViewType::kEditor));
		if (vst3View == NULL)
			return false;
		vst3View->setFrame(static_cast<IPlugFrame*>(vst3HostContext.get()));

		// Hand DPI-aware plugins (FabFilter Pro-Q, etc.) the host scale before
		// they lay out, so getSize() returns a rect that matches the real
		// monitor. Plugins without this interface fall back to reading the DPI
		// from their parent window, which is per-monitor aware in this process.
		IPlugViewContentScaleSupport* rawScaleSupport = NULL;
		const tresult scaleResult = vst3View->queryInterface(
			IPlugViewContentScaleSupport::iid,
			(void**)&rawScaleSupport);
		auto scaleSupport = IPtr<IPlugViewContentScaleSupport>::adopt(rawScaleSupport);
		if (scaleResult == kResultOk && scaleSupport)
		{
			scaleSupport->setContentScaleFactor((IPlugViewContentScaleSupport::ScaleFactor)scaleFactor);
		}

		ViewRect rect;
		const bool hasPreAttachSize = vst3View->getSize(&rect) == kResultOk;
		if (hasPreAttachSize)
		{
			physWidth = max<int32>(1, rect.getWidth());
			physHeight = max<int32>(1, rect.getHeight());
			if (width != NULL)
				*width = toLogical(physWidth);
			if (height != NULL)
				*height = toLogical(physHeight);
		}
		tresult platformResult = vst3View->isPlatformTypeSupported(kPlatformTypeHWND);
		if (platformResult != kResultOk && platformResult != kNotImplemented)
		{
			stopEditing();
			return false;
		}
		registerVST3EditorHostWindowClass();
		vst3EditorHostWindow.reset(CreateWindowExW(0, vst3EditorHostWindowClass, L"",
			WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
			0, 0, physWidth, physHeight, hWnd, NULL, GetModuleHandleW(NULL), NULL));
		if (!vst3EditorHostWindow)
		{
			stopEditing();
			return false;
		}
		if (vst3View->attached(vst3EditorHostWindow.get(), kPlatformTypeHWND) != kResultOk)
		{
			stopEditing();
			return false;
		}
		// removed() may only be called on a view that was attached; a failure
		// before this point must not send it (stopEditing checks the flag).
		vst3ViewAttached = true;
		if (vst3View->getSize(&rect) == kResultOk)
		{
			physWidth = max<int32>(1, rect.getWidth());
			physHeight = max<int32>(1, rect.getHeight());
			if (width != NULL)
				*width = toLogical(physWidth);
			if (height != NULL)
				*height = toLogical(physHeight);
		}
		// Size the host window first, then let the view lay out against the
		// final geometry. A view that could not report a size before attach
		// gets no onSize: it never asserted these dimensions as its own.
		SetWindowPos(vst3EditorHostWindow.get(), NULL, 0, 0, physWidth, physHeight, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		if (hasPreAttachSize)
			vst3View->onSize(&rect);
		EnumChildWindows(vst3EditorHostWindow.get(), showChildWindow, 0);
		// Keep the processor flush-ready for the whole editing session so GUI
		// edits reach the component without per-edit activation cycling.
		beginVST3EditorSession();
		// Edits the plug-in raised synchronously while the session was being
		// established stayed queued (the flush guard was held); drain them.
		flushVST3ParameterChanges();
		return true;
	}

	if (effect == NULL)
		return false;

	vst_rect_t* rect;
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, hWnd, 0.0f);
	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);

	if (width != NULL)
		*width = rect->right - rect->left;
	if (height != NULL)
		*height = rect->bottom - rect->top;
	return true;
}

void VSTPluginInstance::doIdle()
{
	if (library->isVST3())
	{
		// No host idle duty exists for VST3: the IPlugView contract gives the
		// host no repaint responsibility - the view's platform representation
		// paints itself. The forced InvalidateRect/UpdateWindow sweep that
		// used to run here repainted the whole plug-in GUI synchronously on
		// every event-loop idle, which starved the Editor's own UI and made
		// interacting with an embedded panel unstable.
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::stopEditing()
{
	if (library->isVST3())
	{
		if (vst3View != NULL)
		{
			if (vst3ViewAttached)
				vst3View->removed();
			vst3ViewAttached = false;
			vst3View->setFrame(NULL);
			vst3View.reset();
		}
		vst3EditorHostWindow.reset();
		endVST3EditorSession();
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect.get(), VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, NULL, 0.0f);
}
