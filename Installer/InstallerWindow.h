/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The installer's window: a fixed-size, dark, Direct2D/DirectWrite-drawn
	surface that renders the InstallerUi::Model as a four-step timeline with
	a real progress bar and an in-window error panel. Everything it uses
	ships with Windows (d2d1, dwrite, windowscodecs, dwmapi), so the
	installer stays a single dependency-free binary.

	Threading contract: create() and runMessageLoop() run on the main
	thread. update()/finish() are for the worker thread; update() locks the
	model, applies the mutation and posts a repaint.

	Closing the window is not a cancel (audit #348 TD-52, the maintainer's
	decision): while the flow runs, closing only hides the window, and the
	install carries on to the Velopack hand-off. finish() then ends the
	session quietly on success, or shows the window again on a failure so
	the error is never swallowed.
*/

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "../platform/windows/ComPtr.h"
#include "InstallerUiModel.h"

struct ID2D1Factory;
struct ID2D1HwndRenderTarget;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace InstallerUi
{
// Process exit codes (documented in docs/AutoDetectInstaller.md). 5 meant
// "closed mid-download" while closing still cancelled; it is retired, not
// reused.
enum ExitCode
{
	kExitSuccess = 0,
	kExitDownloadFailed = 2,
	kExitLaunchFailed = 3,
	kExitVerifyFailed = 4
};

// The DirectWrite formats one render pass needs; shared between the live
// window and the --ui-shot offscreen renderer.
struct TextFormats
{
	winutil::ComPtr<IDWriteTextFormat> title;
	winutil::ComPtr<IDWriteTextFormat> titleTag;
	winutil::ComPtr<IDWriteTextFormat> subtitle;
	winutil::ComPtr<IDWriteTextFormat> stepTitle;
	winutil::ComPtr<IDWriteTextFormat> stepDetail;
	winutil::ComPtr<IDWriteTextFormat> stepDetailRight;
	winutil::ComPtr<IDWriteTextFormat> footer;
	winutil::ComPtr<IDWriteTextFormat> button;
};

class InstallerWindow
{
public:
	InstallerWindow();
	~InstallerWindow();
	InstallerWindow(const InstallerWindow&) = delete;
	InstallerWindow& operator=(const InstallerWindow&) = delete;

	// Creates and shows the window. false means the window or the Direct2D
	// pipeline could not be created and the caller should fall back to the
	// headless flow.
	bool create(HINSTANCE instance);

	// The calling thread's message loop; it ends when the window is
	// destroyed, which is never before finish().
	static void runMessageLoop();

	// Worker-thread side. Locks the model, mutates it, schedules a repaint.
	void update(const std::function<void(Model&)>& mutate);

	// Ends the session. closeDelayMs > 0 is the success hand-off: the window
	// closes itself after the delay, or at once if the user already closed
	// it. 0 is a failure: the window stays (or comes back) with the error
	// panel until the user closes it or uses the panel's buttons.
	void finish(unsigned closeDelayMs);

	// Renders the fixed preview states as PNGs into outDir (--ui-shot).
	// The caller must have initialized COM (WIC). Returns false when any
	// state failed to render or save.
	static bool renderShots(const std::wstring& outDir);

private:
	// DIP rectangle mirror so the header stays free of Direct2D types.
	struct RectF
	{
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	// Interactive regions the last paint laid out, in DIPs. 0 = none.
	enum HitTarget
	{
		kHitNone = 0,
		kHitReleasesButton = 1,
		kHitCloseButton = 2,
		kHitRepoLink = 3
	};

	static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
	bool createDeviceResources();
	void paint();
	int hitTest(int xPx, int yPx) const;
	void updateAnimationTimer(bool anyStepActive);

	HWND hwnd = nullptr;
	winutil::ComPtr<ID2D1Factory> d2dFactory;
	winutil::ComPtr<IDWriteFactory> dwriteFactory;
	winutil::ComPtr<ID2D1HwndRenderTarget> renderTarget;
	TextFormats formats;
	UINT dpi = 96;
	bool animationTimerRunning = false;

	mutable std::mutex modelMutex;
	Model model;

	// Both are touched on the window thread only (WM_CLOSE, kMsgFinish).
	bool closedWhileRunning = false;
	bool sessionFinished = false;

	// Laid out during paint (under the model lock), hit-tested on mouse
	// messages on the same thread.
	RectF releasesButtonRect;
	RectF closeButtonRect;
	RectF repoLinkRect;
	bool errorPanelVisible = false;
	int hoverTarget = kHitNone;
};
}
