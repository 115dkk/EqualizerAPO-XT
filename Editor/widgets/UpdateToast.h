/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;

// A small, dismissible notice anchored to the bottom centre of the main
// window. First user: the Velopack auto-update flow, which stages updates
// silently in the background - without this notice the user has no way to
// know an update was downloaded and applies on exit. The widget paints a token-driven card
// itself (rounded panel + border) so it reads correctly on every skin even
// before a skin styles it; skins refine it through QSS on #UpdateToast,
// #UpdateToastLabel and #UpdateToastClose.
class UpdateToast : public QWidget
{
	Q_OBJECT

public:
	explicit UpdateToast(QWidget* host);

	// Shows the message, repositions against the host and starts the
	// auto-hide countdown (the close button hides it immediately). An
	// autoHideMs of 0 or less keeps it up until hideMessage() or the close
	// button.
	void showMessage(const QString& message, int autoHideMs = 15000);
	// Hides the notice and stops any pending auto-hide.
	void hideMessage();

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	// The room a wrapped notice leaves on each side of the host.
	static constexpr int kHostMargin = 24;

	// Sizes the toast to its message: one line when it fits the host,
	// wrapped at the host's width less the margins when it does not.
	void fitToHost();
	// Stacks every visible toast on the host bottom-up, the most recently
	// raised on top, so two notices never cover each other.
	void reposition();

	QLabel* label = nullptr;
	QToolButton* closeButton = nullptr;
	QTimer* autoHideTimer = nullptr;
};
