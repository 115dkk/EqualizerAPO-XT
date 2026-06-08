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

bool VSTPluginInstance::startEditing(HWND hWnd, short* width, short* height)
{
	if (width != NULL)
		*width = 400;
	if (height != NULL)
		*height = 300;

	if (library->isVST3())
	{
		if (vst3Controller == NULL)
			return false;
		stopEditing();
		vst3View = vst3Controller->createView(ViewType::kEditor);
		if (vst3View == NULL)
			return false;
		vst3View->setFrame(vst3HostContext);
		ViewRect rect;
		if (vst3View->getSize(&rect) == kResultOk)
		{
			if (width != NULL)
				*width = (short)max<int32>(1, rect.getWidth());
			if (height != NULL)
				*height = (short)max<int32>(1, rect.getHeight());
		}
		tresult platformResult = vst3View->isPlatformTypeSupported(kPlatformTypeHWND);
		if (platformResult != kResultOk && platformResult != kNotImplemented)
		{
			stopEditing();
			return false;
		}
		registerVST3EditorHostWindowClass();
		vst3EditorHostWindow = CreateWindowExW(0, vst3EditorHostWindowClass, L"",
			WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
			0, 0, *width, *height, hWnd, NULL, GetModuleHandleW(NULL), NULL);
		if (vst3EditorHostWindow == NULL)
		{
			stopEditing();
			return false;
		}
		if (vst3View->attached(vst3EditorHostWindow, kPlatformTypeHWND) != kResultOk)
		{
			stopEditing();
			return false;
		}
		if (vst3View->getSize(&rect) == kResultOk)
		{
			if (width != NULL)
				*width = (short)max<int32>(1, rect.getWidth());
			if (height != NULL)
				*height = (short)max<int32>(1, rect.getHeight());
			vst3View->onSize(&rect);
		}
		SetWindowPos(vst3EditorHostWindow, NULL, 0, 0, *width, *height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		EnumChildWindows(vst3EditorHostWindow, showChildWindow, 0);
		return true;
	}

	if (effect == NULL)
		return false;

	vst_rect_t* rect;
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_OPEN, 0, 0, hWnd, 0.0f);
	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_GET_RECT, 0, 0, &rect, 0.0f);

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
		if (vst3EditorHostWindow != NULL)
		{
			InvalidateRect(vst3EditorHostWindow, NULL, FALSE);
			UpdateWindow(vst3EditorHostWindow);
			EnumChildWindows(vst3EditorHostWindow, [](HWND hWnd, LPARAM) -> BOOL {
				InvalidateRect(hWnd, NULL, FALSE);
				UpdateWindow(hWnd);
				return TRUE;
			}, 0);
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_KEEP_ALIVE, 0, 0, NULL, 0.0f);
}

void VSTPluginInstance::stopEditing()
{
	if (library->isVST3())
	{
		if (vst3View != NULL)
		{
			vst3View->removed();
			vst3View->setFrame(NULL);
			vst3View->release();
			vst3View = NULL;
		}
		if (vst3EditorHostWindow != NULL)
		{
			DestroyWindow(vst3EditorHostWindow);
			vst3EditorHostWindow = NULL;
		}
		return;
	}

	if (effect == NULL)
		return;

	effect->control(effect, VST_EFFECT_OPCODE_EDITOR_CLOSE, 0, 0, NULL, 0.0f);
}
