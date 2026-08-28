/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPoint>
#include <QWidget>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "Editor/widgets/TitleBar.h"

namespace WindowFrameHitTest
{
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

	RECT windowRect;
	GetWindowRect(msg->hwnd, &windowRect);
	const double scale = host->devicePixelRatioF();
	const QPoint rel(
		qRound((GET_X_LPARAM(msg->lParam) - windowRect.left) / scale),
		qRound((GET_Y_LPARAM(msg->lParam) - windowRect.top) / scale));
	const int border = 6;
	if (!host->isMaximized())
	{
		const bool left = rel.x() < border;
		const bool right = rel.x() >= host->width() - border;
		const bool top = rel.y() < border;
		const bool bottom = rel.y() >= host->height() - border;
		if (top && left) *result = HTTOPLEFT;
		else if (top && right) *result = HTTOPRIGHT;
		else if (bottom && left) *result = HTBOTTOMLEFT;
		else if (bottom && right) *result = HTBOTTOMRIGHT;
		else if (top) *result = HTTOP;
		else if (bottom) *result = HTBOTTOM;
		else if (left) *result = HTLEFT;
		else if (right) *result = HTRIGHT;
		else if (titleBar != nullptr && titleBar->isCaptionPoint(titleBar->mapFrom(host, rel)))
			*result = HTCAPTION;
		else
			return false;
		return true;
	}
	if (titleBar != nullptr && titleBar->isCaptionPoint(titleBar->mapFrom(host, rel)))
	{
		*result = HTCAPTION;
		return true;
	}
	return false;
}
}
