/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "UpdateToast.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>

#include "Editor/SkinManager.h"

UpdateToast::UpdateToast(QWidget* host)
	: QWidget(host)
{
	setObjectName(QStringLiteral("UpdateToast"));
	setAttribute(Qt::WA_StyledBackground, false);
	setVisible(false);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(14, 8, 8, 8);
	layout->setSpacing(10);

	label = new QLabel(this);
	label->setObjectName(QStringLiteral("UpdateToastLabel"));
	layout->addWidget(label);

	closeButton = new QToolButton(this);
	closeButton->setObjectName(QStringLiteral("UpdateToastClose"));
	closeButton->setAutoRaise(true);
	closeButton->setText(QString(QChar(0x00D7))); // multiplication sign as a close glyph
	closeButton->setToolTip(tr("Dismiss"));
	connect(closeButton, &QToolButton::clicked, this, &UpdateToast::hideMessage);
	layout->addWidget(closeButton, 0, Qt::AlignTop);

	autoHideTimer = new QTimer(this);
	autoHideTimer->setObjectName(QStringLiteral("UpdateToastAutoHide"));
	autoHideTimer->setSingleShot(true);
	connect(autoHideTimer, &QTimer::timeout, this, &UpdateToast::hideMessage);

	host->installEventFilter(this);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens& tokens) {
		label->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.text));
		update();
	});
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	label->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.text));
}

void UpdateToast::showMessage(const QString& message, int autoHideMs)
{
	label->setText(message);
	fitToHost();
	show();
	raise();
	reposition();
	autoHideTimer->stop();
	if (autoHideMs > 0)
		autoHideTimer->start(autoHideMs);
}

void UpdateToast::fitToHost()
{
	// One line while the message fits the host; otherwise the label wraps at
	// the host's width so a long notice stays inside a narrow window instead
	// of running past both edges.
	label->setWordWrap(false);
	setMinimumWidth(0);
	setMaximumWidth(QWIDGETSIZE_MAX);
	adjustSize();
	QWidget* host = parentWidget();
	if (host == nullptr)
		return;
	const int cap = host->width() - 2 * kHostMargin;
	if (cap <= 0 || width() <= cap)
		return;
	label->setWordWrap(true);
	setFixedWidth(cap);
	resize(cap, layout()->heightForWidth(cap));
}

void UpdateToast::hideMessage()
{
	autoHideTimer->stop();
	hide();
	// Lets a toast that sat above this one drop back to the bottom.
	reposition();
}

void UpdateToast::paintEvent(QPaintEvent*)
{
	// Token-driven default card so the notice reads on every skin without a
	// dedicated QSS block: a raised surface, a hairline border and a slim
	// accent keel on the left edge as the "news" marker.
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const int radius = qMax(2, tokens.borderRadius - 2);
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(QColor(tokens.surfaceRaised));
	painter.drawRoundedRect(frame, radius, radius);

	QPainterPath keel;
	keel.addRoundedRect(QRectF(frame.left(), frame.top(), 4, frame.height()), 2, 2);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(tokens.accent));
	painter.drawPath(keel);
}

bool UpdateToast::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parentWidget() && event->type() == QEvent::Resize && !isHidden())
	{
		fitToHost();
		reposition();
	}
	return QWidget::eventFilter(watched, event);
}

void UpdateToast::reposition()
{
	QWidget* host = parentWidget();
	if (host == nullptr)
		return;
	// isHidden rather than isVisible: the gallery shows toasts on a host that
	// is never shown itself. Children are in stacking order, which raise()
	// updates, so the notice shown last sits on top.
	int bottom = host->height() - 18;
	const QList<UpdateToast*> toasts = host->findChildren<UpdateToast*>(Qt::FindDirectChildrenOnly);
	for (UpdateToast* toast : toasts)
	{
		if (toast->isHidden())
			continue;
		const int x = (host->width() - toast->width()) / 2;
		const int y = bottom - toast->height();
		toast->move(qMax(0, x), qMax(0, y));
		bottom = y - 8;
	}
}
