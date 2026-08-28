/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Skinned window chrome for dialogs. The main window replaces the native
	Windows caption with the skinnable TitleBar strip (MainWindow.Frame.cpp);
	a native-captioned dialog inside that session breaks the illusion, so
	this helper mounts the same treatment on any QDialog: the WS_CAPTION
	area is reclaimed as client space (DWM snap, animations and native
	resize stay intact, same WM_NCCALCSIZE / WM_NCHITTEST recipe as the
	main window) and a dialog-mode TitleBar (title text + the conventional
	close X, no minimize/maximize) is inserted above the dialog's layout.
	QDialog::close on an exec()ed dialog rejects it, matching Cancel.

	Do not add a minimize button here (reviewed and rejected, 2026-07-17):
	the platform's own file dialogs carry only the X, and these dialogs are
	application-modal - minimizing one would leave the blocked main window
	looking dead while the owned dialog has no taskbar entry to bring it
	back. Moving the dialog aside (drag) and Alt-Tab already cover the
	"look behind it" need. Revisit only if this chrome is ever mounted on a
	modeless window.
*/

#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

class QDialog;
class QEvent;
class TitleBar;

class DialogChrome : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT

public:
	// Mount the chrome on a dialog (idempotent). The chrome object parents
	// itself to the dialog and unregisters its native filter on destruction.
	static void attach(QDialog* dialog);

	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

protected:
	// Watches the dialog for show/hide so the app-wide native filter is only
	// installed while the dialog is actually visible. It is removed on hide,
	// before Qt tears the native window down: leaving it installed through
	// destruction let a teardown message reach nativeEventFilter, whose
	// per-message winId() lookup recreated a zombie native window mid-destroy
	// and hung the Editor (custom-frame dialogs only). A later show puts it
	// back so a reused dialog keeps its frame.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	explicit DialogChrome(QDialog* dialog);
	~DialogChrome() override;

	void setNativeFilter(bool installed);

	QDialog* dialog = nullptr;
	TitleBar* titleBar = nullptr;
	// The dialog's native handle, cached at construction. nativeEventFilter
	// compares against this instead of calling winId() per message: winId()
	// on a widget being destroyed recreates its native window (see the
	// eventFilter note above).
	quintptr hostWinId = 0;
	bool nativeFilterInstalled = false;
};
