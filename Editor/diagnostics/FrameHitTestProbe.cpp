/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FrameHitTestProbe.h"

#include <cstdio>
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMenuBar>
#include <QTimer>
#include <QToolButton>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Editor/MainWindow.h"
#include "Editor/MainWindowProbe.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/widgets/TitleBar.h"

namespace
{
const char* codeName(LRESULT code)
{
	switch (code)
	{
	case HTTRANSPARENT: return "HTTRANSPARENT";
	case HTCLIENT: return "HTCLIENT";
	case HTCAPTION: return "HTCAPTION";
	case HTLEFT: return "HTLEFT";
	case HTRIGHT: return "HTRIGHT";
	case HTTOP: return "HTTOP";
	case HTBOTTOM: return "HTBOTTOM";
	case HTTOPLEFT: return "HTTOPLEFT";
	case HTTOPRIGHT: return "HTTOPRIGHT";
	default: return "other";
	}
}

// The innermost child window of the probe's window at a screen point, the
// way Windows walks it for mouse input, but blind to other applications'
// windows that may cover the probe's on a desktop.
HWND innermostChildAt(HWND top, POINT screen)
{
	HWND current = top;
	for (;;)
	{
		POINT client = screen;
		ScreenToClient(current, &client);
		HWND child = ChildWindowFromPointEx(current, client, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (child == nullptr || child == current)
			return current;
		current = child;
	}
}

LRESULT hitTest(HWND hwnd, POINT screen)
{
	return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
}

// The deepest visible widget inside the main window itself. Popups (combo
// lists, menus) are top levels of their own: a winId() inside one stops the
// native cascade at the popup and never reaches the chrome host.
QWidget* deepestDescendant(QWidget* root)
{
	QWidget* deepest = nullptr;
	int deepestDepth = -1;
	for (QWidget* widget : root->findChildren<QWidget*>())
	{
		if (widget->window() != root->window() || !widget->isVisible())
			continue;
		int depth = 0;
		for (QWidget* parent = widget; parent != nullptr && parent != root; parent = parent->parentWidget())
			depth++;
		if (depth > deepestDepth)
		{
			deepest = widget;
			deepestDepth = depth;
		}
	}
	return deepest;
}

QAction* otherSkinAction(const MainWindow& window)
{
	const QString current = SkinManager::instance()->currentSkinId();
	const QStringList skins = SkinThemeData::ids();
	for (QAction* action : window.findChildren<QAction*>())
	{
		const QString id = action->data().toString();
		if (action->isCheckable() && action->isEnabled() && id != current && skins.contains(id))
			return action;
	}
	return nullptr;
}

void runChecks(MainWindow& window, TitleBar* titleBar, QWidget* chromeHost)
{
	int failures = 0;
	const auto expect = [&failures](bool ok, const char* what) {
		fprintf(stderr, "  %-58s %s\n", what, ok ? "ok" : "FAILED");
		if (!ok)
			failures++;
	};

	// The precondition: without a native chrome host the probe no longer
	// reproduces the reported state and would pass for the wrong reason.
	expect(chromeHost->internalWinId() != 0, "the native cascade reached the chrome host");

	const HWND host = reinterpret_cast<HWND>(window.winId());
	RECT windowRect;
	GetWindowRect(host, &windowRect);
	const double scale = window.devicePixelRatioF();
	const auto screenPoint = [&](const QWidget* widget, QPoint local) {
		const QPoint inWindow = widget->mapTo(&window, local);
		return POINT { windowRect.left + qRound(inWindow.x() * scale), windowRect.top + qRound(inWindow.y() * scale) };
	};

	struct Case
	{
		const char* name = nullptr;
		POINT point {};
		LRESULT hostCode = HTCLIENT;  // HTCLIENT: the point belongs to the content
	};
	QPoint captionPoint(titleBar->width() / 3, titleBar->height() / 2);
	while (!titleBar->isCaptionPoint(captionPoint) && captionPoint.x() > 8)
		captionPoint.rx() -= 8;
	const QToolButton* closeButton = titleBar->findChild<QToolButton*>(QStringLiteral("TitleBarClose"));
	const QMenuBar* menuBar = window.findChild<QMenuBar*>();
	const QWidget* central = window.centralWidget();
	const Case cases[] = {
		{ "caption", screenPoint(titleBar, captionPoint), HTCAPTION },
		{ "top edge", screenPoint(&window, QPoint(window.width() / 2, 2)), HTTOP },
		{ "left edge", screenPoint(&window, QPoint(2, window.height() / 2)), HTLEFT },
		{ "right edge", screenPoint(&window, QPoint(window.width() - 3, window.height() / 2)), HTRIGHT },
		{ "close button", screenPoint(closeButton, closeButton->rect().center()), HTCLIENT },
		{ "menu bar", screenPoint(menuBar, menuBar->actionGeometry(menuBar->actions().value(0)).center()), HTCLIENT },
		{ "content", screenPoint(central, central->rect().center()), HTCLIENT },
	};

	for (const Case& c : cases)
	{
		const HWND child = innermostChildAt(host, c.point);
		const LRESULT hostCode = hitTest(host, c.point);
		const LRESULT childCode = child != host ? hitTest(child, c.point) : hostCode;
		fprintf(stderr, "%s: child=%s host=%s (child window %s)\n", c.name, codeName(childCode), codeName(hostCode),
			child != host ? "present" : "none");
		char what[96];
		snprintf(what, sizeof(what), "%s: top level answers %s", c.name, codeName(c.hostCode));
		expect(c.hostCode == HTCLIENT ? hostCode == HTCLIENT : hostCode == c.hostCode, what);
		if (child == host)
			continue;
		if (c.hostCode == HTCLIENT)
		{
			snprintf(what, sizeof(what), "%s: covering child keeps HTCLIENT", c.name);
			expect(childCode == HTCLIENT, what);
		}
		else
		{
			snprintf(what, sizeof(what), "%s: covering child hands over (HTTRANSPARENT)", c.name);
			expect(childCode == HTTRANSPARENT, what);
		}
	}
	// The caption must actually be covered, or the handover above was not
	// exercised.
	expect(innermostChildAt(host, cases[0].point) != host, "a native child covers the caption");

	fprintf(stderr, "Frame hit-test probe: %s (%d failed)\n", failures == 0 ? "PASS" : "FAIL", failures);
	// EAPO_FRAME_PROBE_LINGER=1 leaves the window open in this state, so the
	// caption and the edges can be dragged by hand (or by an input script).
	if (qEnvironmentVariableIntValue("EAPO_FRAME_PROBE_LINGER") == 1)
	{
		fprintf(stderr, "Frame hit-test probe: lingering; close the window to exit\n");
		return;
	}
	QCoreApplication::exit(failures == 0 ? 0 : 1);
}
}

bool FrameHitTestProbe::arm(MainWindow& window)
{
	if (QGuiApplication::platformName() != QLatin1String("windows"))
	{
		fprintf(stderr, "Frame hit-test probe: needs the windows platform plugin, not %s\n",
			qPrintable(QGuiApplication::platformName()));
		return false;
	}
	TitleBar* titleBar = window.findChild<TitleBar*>();
	QWidget* chromeHost = window.findChild<QWidget*>(QStringLiteral("WindowChromeHost"));
	if (titleBar == nullptr || chromeHost == nullptr || window.centralWidget() == nullptr)
	{
		fprintf(stderr, "Frame hit-test probe: the custom frame is off (interface/nativeTitleBar or heritage rows)\n");
		return false;
	}
	window.probe().suppressPreferenceWrites();
	window.showNormal();
	window.resize(1024, 768);
	QTimer::singleShot(750, &window, [&window, titleBar, chromeHost]() {
		// What VSTPluginSession::embedPlugin does to its host widget.
		if (QWidget* deepest = deepestDescendant(window.centralWidget()))
			deepest->winId();
		// The report's trigger: a skin switch rebuilds every row.
		if (QAction* skin = otherSkinAction(window))
		{
			fprintf(stderr, "Frame hit-test probe: switching to %s\n", qPrintable(skin->data().toString()));
			skin->trigger();
		}
		QTimer::singleShot(750, &window, [&window, titleBar, chromeHost]() {
			runChecks(window, titleBar, chromeHost);
		});
	});
	return true;
}
