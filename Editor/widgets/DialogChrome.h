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
*/

#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

class QDialog;
class TitleBar;

class DialogChrome : public QObject, public QAbstractNativeEventFilter
{
	Q_OBJECT

public:
	// Mount the chrome on a dialog (idempotent). The chrome object parents
	// itself to the dialog and unregisters its native filter on destruction.
	static void attach(QDialog* dialog);

	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
	explicit DialogChrome(QDialog* dialog);
	~DialogChrome() override;

	QDialog* dialog = nullptr;
	TitleBar* titleBar = nullptr;
};
