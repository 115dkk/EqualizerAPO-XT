#include "EditableValue.h"

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

QString EditableValue::unit() const
{
	return currentUnit;
}

void EditableValue::setUnit(const QString& unit)
{
	currentUnit = unit;
	refreshText();
}

void EditableValue::mouseDoubleClickEvent(QMouseEvent* event)
{
	QWidget::mouseDoubleClickEvent(event);
	editField->setText(QLocale::c().toString(currentValue, 'f', 2));
	stack->setCurrentWidget(editField);
	editField->setFocus();
	editField->selectAll();
}

void EditableValue::commitEdit()
{
	if (stack->currentWidget() != editField)
		return;

	bool ok = false;
	double parsedValue = QLocale::c().toDouble(editField->text(), &ok);
	if (ok)
	{
		currentValue = parsedValue;
		emit valueChanged(currentValue);
	}

	stack->setCurrentWidget(displayLabel);
	refreshText();
}

void EditableValue::refreshText()
{
	QString text = QLocale::c().toString(currentValue, 'f', 1);
	if (!currentUnit.isEmpty())
		text += QStringLiteral(" ") + currentUnit;
	displayLabel->setText(text);
}
