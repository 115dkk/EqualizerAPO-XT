/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "DialogChrome.h"

#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "TitleBar.h"
#include "Editor/helpers/WindowFrameHitTest.h"

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

	// Force the native window into existence, then cache its handle so
	// nativeEventFilter never has to call winId() again (see the header).
	hostWinId = dialog->winId();
	setNativeFilter(true);

	// Watch the dialog itself so the native filter comes back out on hide,
	// before Qt destroys the native window.
	dialog->installEventFilter(this);

	// Recalculate the non-client area now that WM_NCCALCSIZE is handled.
	SetWindowPos(reinterpret_cast<HWND>(hostWinId), nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

DialogChrome::~DialogChrome()
{
	setNativeFilter(false);
}

void DialogChrome::setNativeFilter(bool installed)
{
	if (installed == nativeFilterInstalled)
		return;
	if (installed)
		QCoreApplication::instance()->installNativeEventFilter(this);
	else
		QCoreApplication::instance()->removeNativeEventFilter(this);
	nativeFilterInstalled = installed;
}

bool DialogChrome::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == dialog)
	{
		// Hide fires on every close path (accept, reject, Esc, the custom X)
		// before Qt destroys the native window, so dropping the native filter
		// here keeps every teardown message away from nativeEventFilter. A
		// later show (a reused dialog) reinstates it.
		if (event->type() == QEvent::Hide)
			setNativeFilter(false);
		else if (event->type() == QEvent::Show)
		{
			hostWinId = dialog->winId();
			setNativeFilter(true);
		}
	}
	return QObject::eventFilter(watched, event);
}

bool DialogChrome::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
	if (eventType != QByteArrayLiteral("windows_generic_MSG"))
		return false;

	MSG* msg = static_cast<MSG*>(message);
	if (dialog == nullptr || msg->hwnd != reinterpret_cast<HWND>(hostWinId))
		return false;

	return WindowFrameHitTest::handle(dialog, titleBar, msg, result);
}
