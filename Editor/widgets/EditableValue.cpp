/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditableValue.h"
#include "EditableValueText.h"

#include <QLocale>
#include <QMouseEvent>

EditableValue::EditableValue(QWidget* parent)
	: QWidget(parent)
{
	stack = new QStackedLayout(this);
	stack->setContentsMargins(0, 0, 0, 0);

	displayLabel = new QLabel(this);
	displayLabel->setObjectName(QStringLiteral("EditableValue"));
	displayLabel->setAlignment(Qt::AlignCenter);
	displayLabel->setMinimumWidth(62);

	editField = new QLineEdit(this);
	editField->setObjectName(QStringLiteral("EditableValueEditor"));
	editField->setAlignment(Qt::AlignCenter);
	editField->hide();
	connect(editField, SIGNAL(editingFinished()), this, SLOT(commitEdit()));

	stack->addWidget(displayLabel);
	stack->addWidget(editField);
	refreshText();
}

double EditableValue::value() const
{
	return currentValue;
}

void EditableValue::setValue(double value)
{
	currentValue = value;
	// The value moved elsewhere (the knob, a reset) while the field was open.
	// What the field holds is now older than the value, and the field used to
	// commit it as soon as it lost the focus: the close prompt taking the
	// focus wrote the old value back over the change, and that prompt's Save
	// stored it (#459). The field closes on the new value instead, and a
	// draft typed into it before the change is dropped: the change came later.
	if (stack->currentWidget() == editField)
	{
		editField->setModified(false);
		stack->setCurrentWidget(displayLabel);
	}
	refreshText();
}

const QString& EditableValue::unit() const
{
	return currentUnit;
}

void EditableValue::setUnit(const QString& unit)
{
	currentUnit = unit;
	refreshText();
}

void EditableValue::setDecimals(int decimals)
{
	displayDecimals = decimals;
	refreshText();
}

void EditableValue::mouseDoubleClickEvent(QMouseEvent* event)
{
	QWidget::mouseDoubleClickEvent(event);
	editField->setText(QLocale::c().toString(currentValue, 'f', qMax(2, displayDecimals)));
	stack->setCurrentWidget(editField);
	editField->setFocus();
	editField->selectAll();
}

void EditableValue::commitEdit()
{
	if (stack->currentWidget() != editField)
		return;

	// Only what the user typed is a new value. A field opened and left as it
	// was shows the value rounded to its decimals, and committing that would
	// change the value without anyone asking. The flag is cleared first:
	// closing the field takes its focus, which calls this again.
	const bool typed = editField->isModified();
	editField->setModified(false);
	double parsedValue = 0.0;
	if (typed && parseEditableValueText(editField->text(), QLocale::system(), &parsedValue))
	{
		currentValue = parsedValue;
		emit valueChanged(currentValue);
	}

	stack->setCurrentWidget(displayLabel);
	refreshText();
}

void EditableValue::refreshText()
{
	QString text = QLocale::c().toString(currentValue, 'f', displayDecimals);
	if (!currentUnit.isEmpty())
		text += QStringLiteral(" ") + currentUnit;
	displayLabel->setText(text);
}
