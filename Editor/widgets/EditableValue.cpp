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

	double parsedValue = 0.0;
	if (parseEditableValueText(editField->text(), QLocale::system(), &parsedValue))
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
