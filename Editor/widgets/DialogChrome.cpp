/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "DialogChrome.h"

#include <QCoreApplication>
#include <QDialog>
#include <QLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "TitleBar.h"

void DialogChrome::attach(QDialog* dialog)
{
	if (dialog == nullptr || dialog->layout() == nullptr)
		return;
	if (dialog->findChild<DialogChrome*>(QString(), Qt::FindDirectChildrenOnly) != nullptr)
		return;
	new DialogChrome(dialog);
}

DialogChrome::DialogChrome(QDialog* dialog)
	: QObject(dialog), dialog(dialog)
{
	titleBar = new TitleBar(dialog, dialog, /*dialogMode=*/ true);
	// QLayout::setMenuBar parks the strip above the layout's content area,
	// full width, without disturbing the dialog's own grid.
	dialog->layout()->setMenuBar(titleBar);

	// Force the native window into existence so the filter has a handle to
	// match and the frame change below reaches the right window.
	HWND hwnd = reinterpret_cast<HWND>(dialog->winId());
	QCoreApplication::instance()->installNativeEventFilter(this);

	// Recalculate the non-client area now that WM_NCCALCSIZE is handled.
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

DialogChrome::~DialogChrome()
{
	QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool DialogChrome::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
	if (eventType != QByteArrayLiteral("windows_generic_MSG"))
		return false;

	MSG* msg = static_cast<MSG*>(message);
	if (dialog == nullptr || msg->hwnd != reinterpret_cast<HWND>(dialog->winId()))
		return false;

	// The recipe mirrors MainWindow::nativeEvent (MainWindow.Frame.cpp):
	// keep WS_CAPTION/WS_THICKFRAME so DWM snap and native resize survive,
	// but claim the caption area as client space and answer hit tests for
	// the strip and the resize borders ourselves.
	switch (msg->message)
	{
	case WM_NCCALCSIZE:
	{
		if (msg->wParam == TRUE)
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
		break;
	}
	case WM_NCHITTEST:
	{
		RECT windowRect;
		GetWindowRect(msg->hwnd, &windowRect);
		const double scale = dialog->devicePixelRatioF();
		const QPoint rel(
			qRound((GET_X_LPARAM(msg->lParam) - windowRect.left) / scale),
			qRound((GET_Y_LPARAM(msg->lParam) - windowRect.top) / scale));

		const int border = 6;
		if (!dialog->isMaximized())
		{
			const bool left = rel.x() < border;
			const bool right = rel.x() >= dialog->width() - border;
			const bool top = rel.y() < border;
			const bool bottom = rel.y() >= dialog->height() - border;
			if (top && left) { *result = HTTOPLEFT; return true; }
			if (top && right) { *result = HTTOPRIGHT; return true; }
			if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
			if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
			if (top) { *result = HTTOP; return true; }
			if (bottom) { *result = HTBOTTOM; return true; }
			if (left) { *result = HTLEFT; return true; }
			if (right) { *result = HTRIGHT; return true; }
		}

		if (titleBar != nullptr)
		{
			const QPoint inTitleBar = titleBar->mapFrom(dialog, rel);
			if (titleBar->isCaptionPoint(inTitleBar))
			{
				*result = HTCAPTION;
				return true;
			}
		}
		break;
	}
	default:
		break;
	}

	return false;
}
