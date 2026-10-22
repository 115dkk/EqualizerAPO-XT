/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QCoreApplication>
#include <QPoint>
#include <QWidget>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "Editor/widgets/TitleBar.h"

namespace WindowFrameHitTest
{
// What the custom frame answers for a WM_NCHITTEST point given in screen
// pixels: a resize edge, the caption, or HTCLIENT when the point belongs to
// the window's content (the caption buttons included).
inline LRESULT frameCode(QWidget* host, TitleBar* titleBar, HWND hwnd, LPARAM screenPoint)
{
	RECT windowRect;
	GetWindowRect(hwnd, &windowRect);
	const double scale = host->devicePixelRatioF();
	const QPoint rel(
		qRound((GET_X_LPARAM(screenPoint) - windowRect.left) / scale),
		qRound((GET_Y_LPARAM(screenPoint) - windowRect.top) / scale));
	const int border = 6;
	if (!host->isMaximized())
	{
		const bool left = rel.x() < border;
		const bool right = rel.x() >= host->width() - border;
		const bool top = rel.y() < border;
		const bool bottom = rel.y() >= host->height() - border;
		if (top && left) return HTTOPLEFT;
		if (top && right) return HTTOPRIGHT;
		if (bottom && left) return HTBOTTOMLEFT;
		if (bottom && right) return HTBOTTOMRIGHT;
		if (top) return HTTOP;
		if (bottom) return HTBOTTOM;
		if (left) return HTLEFT;
		if (right) return HTRIGHT;
	}
	if (titleBar != nullptr && titleBar->isCaptionPoint(titleBar->mapFrom(host, rel)))
		return HTCAPTION;
	return HTCLIENT;
}

inline bool handle(QWidget* host, TitleBar* titleBar, MSG* msg, qintptr* result)
{
	if (host == nullptr || msg == nullptr || result == nullptr)
		return false;
	if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE)
	{
		NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
		if (IsZoomed(msg->hwnd))
		{
			const int frame = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
			params->rgrc[0].left += frame;
			params->rgrc[0].top += frame;
			params->rgrc[0].right -= frame;
			params->rgrc[0].bottom -= frame;
		}
		*result = 0;
		return true;
	}
	if (msg->message != WM_NCHITTEST)
		return false;

	const LRESULT code = frameCode(host, titleBar, msg->hwnd, msg->lParam);
	if (code == HTCLIENT)
		return false;
	*result = code;
	return true;
}

// Once one widget asks for its winId() (the VST panel embedded in a card
// does), Qt makes its whole parent chain native and, by default, every
// sibling along that chain too. The chrome host (title strip + menu bar) is
// a sibling of the central widget, so it becomes a child HWND covering the
// caption. Windows sends WM_NCHITTEST to the topmost window under the
// pointer; that child answered HTCLIENT and the drag never reached the
// frame, and the side edges went dead the same way over the central widget.
// For the frame's own points a child answers HTTRANSPARENT instead, so
// Windows asks the window beneath it in the same thread, ending at the top
// level, whose handle() above returns the caption or edge code.
inline bool forwardFromChild(QWidget* host, TitleBar* titleBar, HWND hostHwnd, MSG* msg, qintptr* result)
{
	if (host == nullptr || msg == nullptr || result == nullptr)
		return false;
	if (msg->message != WM_NCHITTEST || msg->hwnd == hostHwnd || GetAncestor(msg->hwnd, GA_ROOT) != hostHwnd)
		return false;
	if (frameCode(host, titleBar, hostHwnd, msg->lParam) == HTCLIENT)
		return false;
	*result = HTTRANSPARENT;
	return true;
}

// Applies forwardFromChild to the native children of one custom-frame
// window; the window's own HWND keeps its nativeEvent path. The handle is
// cached at construction because calling winId() per message would
// recreate a native window that is being destroyed (see DialogChrome).
class ChildHitForwarder : public QAbstractNativeEventFilter
{
public:
	ChildHitForwarder(QWidget* host, TitleBar* titleBar, HWND hostHwnd)
		: host(host), titleBar(titleBar), hostHwnd(hostHwnd)
	{
		QCoreApplication::instance()->installNativeEventFilter(this);
	}

	~ChildHitForwarder() override
	{
		QCoreApplication::instance()->removeNativeEventFilter(this);
	}

	ChildHitForwarder(const ChildHitForwarder&) = delete;
	ChildHitForwarder& operator=(const ChildHitForwarder&) = delete;

	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
	{
		if (eventType != QByteArrayLiteral("windows_generic_MSG"))
			return false;
		return forwardFromChild(host, titleBar, hostHwnd, static_cast<MSG*>(message), result);
	}

private:
	QWidget* host;
	TitleBar* titleBar;
	HWND hostHwnd;
};
}
