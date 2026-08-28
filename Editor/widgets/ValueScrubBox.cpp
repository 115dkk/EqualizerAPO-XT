/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "ValueScrubBox.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMouseEvent>

// Vertical pixels per singleStep. The Shift ratio is four times finer for
// precise adjustment.
static const int scrubPixelsPerStep = 4;
static const int scrubFinePixelsPerStep = 16;

ValueScrubController::ValueScrubController(QAbstractSpinBox* box)
	: QObject(box), box(box)
{
	// QAbstractSpinBox routes mouse input through its line edit, so the drag
	// gesture has to be intercepted there, not on the spin box itself.
	box->findChild<QLineEdit*>()->installEventFilter(this);
}

ValueScrubController::~ValueScrubController()
{
	endScrub();
}

void ValueScrubController::endScrub()
{
	if (scrubbing)
	{
		scrubbing = false;
		QGuiApplication::restoreOverrideCursor();
	}
}

bool ValueScrubController::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	{
		const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (mouseEvent->button() == Qt::LeftButton)
		{
			pressed = true;
			textDrag = false;
			pressPos = mouseEvent->position().toPoint();
			anchorY = pressPos.y();
		}
		// Let the line edit place the caret as usual.
		return false;
	}
	case QEvent::MouseMove:
	{
		const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (!pressed || !(mouseEvent->buttons() & Qt::LeftButton))
			return false;
		if (textDrag)
			return false;

		const QPoint pos = mouseEvent->position().toPoint();
		if (!scrubbing)
		{
			const int threshold = QApplication::startDragDistance();
			const int dx = qAbs(pos.x() - pressPos.x());
			const int dy = qAbs(pos.y() - pressPos.y());
			if (dy >= threshold && dy > dx)
			{
				scrubbing = true;
				anchorY = pos.y();
				QGuiApplication::setOverrideCursor(Qt::SizeVerCursor);
			}
			else if (dx >= threshold)
			{
				// Horizontal drags keep meaning text selection for this press.
				textDrag = true;
				return false;
			}
			else
			{
				// Gesture not decided yet: swallow the move so the line edit
				// does not start a selection that a scrub would then abandon.
				return true;
			}
		}

		const int pixelsPerStep = (mouseEvent->modifiers() & Qt::ShiftModifier)
			? scrubFinePixelsPerStep : scrubPixelsPerStep;
		const int steps = (anchorY - pos.y()) / pixelsPerStep;
		if (steps != 0)
		{
			anchorY -= steps * pixelsPerStep;
			if (box->isEnabled() && !box->isReadOnly())
				box->stepBy(steps);
		}
		return true;
	}
	case QEvent::MouseButtonRelease:
	{
		const QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
		if (mouseEvent->button() != Qt::LeftButton)
			return false;
		pressed = false;
		textDrag = false;
		if (scrubbing)
		{
			endScrub();
			// Swallow the release so it does not collapse the stepBy() text
			// selection or move the caret after a scrub.
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

ValueScrubBox::ValueScrubBox(QWidget* parent)
	: QDoubleSpinBox(parent)
{
	setButtonSymbols(QAbstractSpinBox::NoButtons);
	// Hook for skins: they may key QSS or paint code off this.
	setProperty("valueScrub", true);
	new ValueScrubController(this);
}

ValueScrubIntBox::ValueScrubIntBox(QWidget* parent)
	: QSpinBox(parent)
{
	setButtonSymbols(QAbstractSpinBox::NoButtons);
	setProperty("valueScrub", true);
	new ValueScrubController(this);
}
