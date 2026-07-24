/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Custom window chrome: the native Windows caption is removed (the window
	keeps its WS_CAPTION/WS_THICKFRAME styles so DWM snap, animations and
	native resize stay intact) and a skinnable TitleBar widget plus the menu
	bar take its place via QMainWindow::setMenuWidget. WM_NCCALCSIZE consumes
	the caption area; WM_NCHITTEST hands back HTCAPTION over the title strip
	and the resize-border codes along the edges, so moving, snapping and
	resizing remain native. interface/nativeTitleBar (registry) is the escape
	hatch: machines where custom chrome misbehaves can restore the stock
	caption with one toggle + restart.
*/

#include <QLayout>
#include <QMenuBar>
#include <QSettings>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "Editor/widgets/TitleBar.h"
#include "Editor/helpers/EditorSettings.h"
#include "Editor/helpers/WindowFrameHitTest.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"

void MainWindow::setupWindowChrome()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	// Heritage (legacy rows) keeps the stock Windows caption: the custom
	// title strip is part of the modern presentation.
	useCustomFrame = !settings.value(QLatin1String(EditorSettings::Keys::NativeTitleBar), false).toBool()
		&& !settings.value(QLatin1String(EditorSettings::Keys::LegacyRows), false).toBool();
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
	if (WindowFrameHitTest::handle(this, titleBar, msg, result))
		return true;

	return QMainWindow::nativeEvent(eventType, message, result);
}
