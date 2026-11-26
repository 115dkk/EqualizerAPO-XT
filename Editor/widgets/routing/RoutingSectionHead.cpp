/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RoutingSectionHead.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QToolButton>

#include "Editor/widgets/ElidedLabel.h"

RoutingSectionHead::RoutingSectionHead(const QString& caption, const QString& captionObjectName, QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("RoutingSectionHead"));

	row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(8);

	// The card header's fold button, one size down in importance but not in
	// form: the same object name and the same headerAction property, so every
	// skin gives it the bare glyph at rest and its own cap under the pointer.
	// Not checkable, for the reason the header's is not (an open section is
	// where a section rests).
	toggle = new QToolButton(this);
	toggle->setObjectName(QStringLiteral("FilterCardIconButton"));
	toggle->setProperty("headerAction", true);
	toggle->setProperty("routingSectionFold", true);
	connect(toggle, &QToolButton::clicked, this, &RoutingSectionHead::toggleClicked);
	row->addWidget(toggle);

	// The caption folds too: the button is small and the caption is the
	// thing the eye finds first.
	captionLabel = new QLabel(caption, this);
	captionLabel->setObjectName(captionObjectName);
	captionLabel->installEventFilter(this);
	row->addWidget(captionLabel);

	// The summary takes what the row has left and elides at the end, so the
	// longest routing never widens the card. Its tooltip is the whole text.
	summary = new ElidedLabel(this);
	summary->setObjectName(QStringLiteral("RoutingSectionSummary"));
	summary->setElideMode(Qt::ElideRight);
	summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	summary->setMinimumWidth(0);
	row->addWidget(summary, 1);
	// While the section is open the summary is hidden, and this takes the
	// row's leftover so the caption and the open controls stay at the left.
	// Folded, the summary's stretch factor gives it the leftover instead.
	row->addStretch(0);

	setSummary(QString());
	apply();
}

void RoutingSectionHead::setBody(QWidget* newBody)
{
	body = newBody;
	if (body != nullptr)
		body->setVisible(!folded);
}

void RoutingSectionHead::addOpenControl(QWidget* control)
{
	if (control == nullptr)
		return;
	openControls.append(control);
	// After the fold button, the caption and the controls added before this
	// one; ahead of the summary.
	row->insertWidget(1 + int(openControls.size()), control);
	control->setVisible(!folded);
}

void RoutingSectionHead::setSummary(const QString& text)
{
	summary->setFullText(text.isEmpty() ? tr("No connections") : text);
}

QString RoutingSectionHead::summaryText() const
{
	return summary->fullText();
}

bool RoutingSectionHead::isFolded() const
{
	return folded;
}

void RoutingSectionHead::setFolded(bool newFolded)
{
	if (folded == newFolded)
		return;
	folded = newFolded;
	apply();
}

QAbstractButton* RoutingSectionHead::foldButton() const
{
	return toggle;
}

bool RoutingSectionHead::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == captionLabel && event->type() == QEvent::MouseButtonRelease
		&& static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton
		&& captionLabel->rect().contains(static_cast<QMouseEvent*>(event)->position().toPoint()))
	{
		toggleClicked();
		return true;
	}
	return QWidget::eventFilter(watched, event);
}

void RoutingSectionHead::toggleClicked()
{
	setFolded(!folded);
	emit foldChosen(folded);
}

void RoutingSectionHead::apply()
{
	// The header's own signs, ASCII so no font decides whether the arrow
	// exists.
	toggle->setText(folded ? QStringLiteral(">") : QStringLiteral("v"));
	toggle->setToolTip(folded ? tr("Expand channel mapping") : tr("Collapse channel mapping"));

	if (folded)
	{
		// A widget that is hidden while it holds the focus hands the focus to
		// the next widget in the chain, which can be a control of another
		// card, and the list then scrolls to it. The fold button takes it
		// instead.
		QWidget* focused = QApplication::focusWidget();
		bool focusLeaves = focused != nullptr && body != nullptr
			&& (focused == body || body->isAncestorOf(focused));
		for (const QPointer<QWidget>& control : openControls)
			if (control != nullptr && focused != nullptr
				&& (focused == control || control->isAncestorOf(focused)))
				focusLeaves = true;
		if (focusLeaves)
			toggle->setFocus(Qt::OtherFocusReason);
	}

	if (body != nullptr)
		body->setVisible(!folded);
	for (const QPointer<QWidget>& control : openControls)
		if (control != nullptr)
			control->setVisible(!folded);
	summary->setVisible(folded);

	// The card pins its body to the editor's height and follows it through
	// the editor's layout requests (FilterCardRow::watchEditorScroll). The
	// request has to cross every layout between this head and the editor;
	// left to itself the chain revalidates one hop per event-loop pass and
	// can keep a stale hint on the way (RoutingView::syncSizeToHint has the
	// same remedy for the channel fold).
	for (QWidget* ancestor = parentWidget(); ancestor != nullptr;
		ancestor = ancestor->parentWidget())
	{
		if (ancestor->layout() != nullptr)
			ancestor->layout()->invalidate();
		ancestor->updateGeometry();
	}
}
