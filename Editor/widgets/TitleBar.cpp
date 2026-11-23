/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TitleBar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QToolButton>

#include "Editor/widgets/TitleBarSkin.h"
#include "platform/qt/TintedIcon.h"

namespace
{
// The installed skin's glyph ink; the palette's text colour when no skin is
// installed (a program that never set one up).
QColor captionInk(const QWidget* widget)
{
	if (const TitleBarSkin* skin = TitleBarSkin::current())
		return skin->ink();
	return widget->palette().color(QPalette::WindowText);
}

QToolButton* makeCaptionButton(const char* objectName, QWidget* parent)
{
	QToolButton* button = new QToolButton(parent);
	button->setObjectName(QLatin1String(objectName));
	button->setFocusPolicy(Qt::NoFocus);
	// Caption buttons are wider than tall, like the native ones, so the hit
	// targets stay comfortable; QSS may restyle freely.
	button->setFixedSize(QSize(40, 30));
	return button;
}
}

TitleBar::TitleBar(QWidget* window, QWidget* parent, bool dialogMode)
	: QWidget(parent), hostWindow(window), dialogMode(dialogMode)
{
	setObjectName(QStringLiteral("AppTitleBar"));
	setAttribute(Qt::WA_StyledBackground, true);
	setFixedHeight(34);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(12, 0, 0, 0);
	layout->setSpacing(0);

	titleLabel = new QLabel(hostWindow->windowTitle(), this);
	titleLabel->setObjectName(QStringLiteral("TitleBarText"));
	layout->addWidget(titleLabel);
	// The title follows the window (dirty markers, file names) live.
	connect(hostWindow, &QWidget::windowTitleChanged, titleLabel, &QLabel::setText);

	layout->addStretch(1);

	if (!dialogMode)
	{
		minimizeButton = makeCaptionButton("TitleBarMin", this);
		connect(minimizeButton, &QToolButton::clicked, hostWindow, &QWidget::showMinimized);
		layout->addWidget(minimizeButton);

		maximizeButton = makeCaptionButton("TitleBarMax", this);
		connect(maximizeButton, &QToolButton::clicked, this, [this]() {
			if (hostWindow->isMaximized())
				hostWindow->showNormal();
			else
				hostWindow->showMaximized();
		});
		layout->addWidget(maximizeButton);
	}

	closeButton = makeCaptionButton("TitleBarClose", this);
	connect(closeButton, &QToolButton::clicked, hostWindow, &QWidget::close);
	layout->addWidget(closeButton);

	// Track maximize/restore from any source (snap, double-click, keyboard).
	hostWindow->installEventFilter(this);

	// Re-tint on every live skin/dark switch. The switch slots (skinSelected,
	// darkThemeToggled) never run MainWindow::applyRedesignPreferences, so
	// without this a light->dark toggle would leave the caption glyphs in the
	// light skin's near-black ink - invisible on the now-dark strip.
	if (TitleBarSkin* skin = TitleBarSkin::current())
		connect(skin, &TitleBarSkin::changed, this, &TitleBar::applySkinIcons);
	applySkinIcons();
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == hostWindow && event->type() == QEvent::WindowStateChange)
		updateMaximizeButton();
	return QWidget::eventFilter(watched, event);
}

void TitleBar::applySkinIcons()
{
	const QColor ink = captionInk(this);
	if (minimizeButton != nullptr)
		minimizeButton->setIcon(TintedIcon::make(QStringLiteral(":/icons/modern/window-min.svg"), ink, 14));
	closeButton->setIcon(TintedIcon::make(QStringLiteral(":/icons/modern/window-close.svg"), ink, 14));
	updateMaximizeButton();
}

void TitleBar::updateMaximizeButton()
{
	if (maximizeButton == nullptr)
		return;
	const QColor ink = captionInk(this);
	const bool maximized = hostWindow->isMaximized();
	maximizeButton->setIcon(TintedIcon::make(
		maximized ? QStringLiteral(":/icons/modern/window-restore.svg")
		: QStringLiteral(":/icons/modern/window-max.svg"), ink, 14));
	maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

bool TitleBar::isCaptionPoint(const QPoint& point) const
{
	if (!rect().contains(point))
		return false;
	const QWidget* child = childAt(point);
	return child == nullptr || child == titleLabel;
}

void TitleBar::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	// The stylesheet background first (the WA_StyledBackground default we are
	// replacing by overriding paintEvent)...
	QStyleOption option;
	option.initFrom(this);
	style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
	// ...then the skin's painted chrome on top, under the child widgets.
	if (const TitleBarSkin* skin = TitleBarSkin::current())
		skin->paintChrome(painter, rect());
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (!dialogMode && isCaptionPoint(event->pos()))
	{
		if (hostWindow->isMaximized())
			hostWindow->showNormal();
		else
			hostWindow->showMaximized();
		event->accept();
		return;
	}
	QWidget::mouseDoubleClickEvent(event);
}
