/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SpinBoxEditFieldFix.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QStyle>
#include <QStyleOption>
#include <QVersionNumber>

namespace
{
// Qt sets the edit field geometry from several places (resize, style and
// button-symbol changes); each of them moves or resizes the line edit, so
// watching those two events catches every path.
class EditFieldFilter : public QObject
{
public:
	using QObject::QObject;

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() != QEvent::Move && event->type() != QEvent::Resize)
			return false;
		QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
		QAbstractSpinBox* box = edit != nullptr ? qobject_cast<QAbstractSpinBox*>(edit->parentWidget()) : nullptr;
		if (box != nullptr)
			restore(box, edit);
		return false;
	}

private:
	static void restore(QAbstractSpinBox* box, QLineEdit* edit)
	{
		// SE_FrameContents answers the spin box's own style sheet rule with
		// its contents rect, under the same condition that sends the edit
		// field down the broken path. That is where the field starts, and
		// without buttons it is the whole field.
		QStyleOption option;
		option.initFrom(box);
		const QRect contents = box->style()->subElementRect(QStyle::SE_FrameContents, &option, box);
		QRect field = edit->geometry();
		if (box->buttonSymbols() == QAbstractSpinBox::NoButtons)
		{
			if (field.width() > 1 || contents.width() <= 1)
				return;
			field = contents;
		}
		else
		{
			// The right edge (the buttons' left side) is already correct.
			if (field.left() != 0 || contents.left() <= 0)
				return;
			field.setLeft(contents.left());
		}
		edit->setGeometry(field);
	}
};
}

void SpinBoxEditFieldFix::installIfAffected(QApplication& application)
{
	if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
		return;
	application.installEventFilter(new EditFieldFilter(&application));
}
