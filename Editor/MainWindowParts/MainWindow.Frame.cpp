/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Custom window chrome: the native Windows caption is removed (the window
	keeps its WS_CAPTION/WS_THICKFRAME styles so DWM snap, animations and
	native resize stay intact) and a skinnable TitleBar widget plus the menu
	bar take its place via QMainWindow::setMenuWidget. WM_NCCALCSIZE consumes
	the caption area; WM_NCHITTEST hands back HTCAPTION over the title strip
	and the resize-border codes along the edges, so moving, snapping and
	resizing remain native. interface/nativeTitleBar (registry) is the escape
	hatch: machines where custom chrome misbehaves (see the PC-bang reports in
	issue #75) can restore the stock caption with one toggle + restart.
*/

#include <QLayout>
#include <QMenuBar>
#include <QSettings>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "Editor/widgets/TitleBar.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"

void MainWindow::setupWindowChrome()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	// Heritage (legacy rows) keeps the stock Windows caption: the custom
	// title strip is part of the modern presentation.
	useCustomFrame = !settings.value("interface/nativeTitleBar", false).toBool()
		&& !settings.value(QStringLiteral("interface/legacyRows"), false).toBool();
	if (!useCustomFrame)
		return;

	// The title strip and the menu bar share the QMainWindow menu-widget slot.
	titleBar = new TitleBar(this);
	QWidget* chromeHost = new QWidget(this);
	chromeHost->setObjectName(QStringLiteral("WindowChromeHost"));
	chromeHost->setAttribute(Qt::WA_StyledBackground, true);
	QVBoxLayout* chromeLayout = new QVBoxLayout(chromeHost);
	chromeLayout->setContentsMargins(0, 0, 0, 0);
	chromeLayout->setSpacing(0);
	chromeLayout->addWidget(titleBar);
	QMenuBar* menu = ui->menuBar;
	menu->setParent(chromeHost);
	chromeLayout->addWidget(menu);
	setMenuWidget(chromeHost);

	// Recalculate the non-client area now that WM_NCCALCSIZE is handled.
	HWND hwnd = reinterpret_cast<HWND>(winId());
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
	if (!useCustomFrame || eventType != QByteArrayLiteral("windows_generic_MSG"))
		return QMainWindow::nativeEvent(eventType, message, result);

	MSG* msg = static_cast<MSG*>(message);
	switch (msg->message)
	{
	case WM_NCCALCSIZE:
	{
		if (msg->wParam == TRUE)
		{
			// Claim the whole window rect as client area (the caption is
			// gone). A maximized window extends past the monitor edge by the
			// frame thickness, so inset all four sides to keep the content
			// on screen.
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
		// Work in window-relative LOGICAL coordinates: physical offset from
		// the window origin divided by the window's device pixel ratio. This
		// avoids mixed-DPI global-coordinate mapping entirely.
		RECT windowRect;
		GetWindowRect(msg->hwnd, &windowRect);
		const double scale = devicePixelRatioF();
		const QPoint rel(
			qRound((GET_X_LPARAM(msg->lParam) - windowRect.left) / scale),
			qRound((GET_Y_LPARAM(msg->lParam) - windowRect.top) / scale));

		const int border = 6;
		if (!isMaximized())
		{
			const bool left = rel.x() < border;
			const bool right = rel.x() >= width() - border;
			const bool top = rel.y() < border;
			const bool bottom = rel.y() >= height() - border;
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
			const QPoint inTitleBar = titleBar->mapFrom(this, rel);
			if (titleBar->isCaptionPoint(inTitleBar))
			{
				// Native caption semantics: drag to move, drag to the screen
				// edge to snap, double-click to maximize/restore.
				*result = HTCAPTION;
				return true;
			}
		}
		break;
	}
	default:
		break;
	}

	return QMainWindow::nativeEvent(eventType, message, result);
}
