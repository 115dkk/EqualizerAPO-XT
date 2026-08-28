/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ScalarKnobCardEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"

ScalarKnobCardEditor::ScalarKnobCardEditor(QWidget* parent)
	: IFilterGUI(parent)
{
}

void ScalarKnobCardEditor::initializeScalarCard(const QString& editorObjectName,
	const QString& knobObjectName, const QString& valueBlockObjectName,
	QWidget* caption, EditableValue* value, const QString& dynamicText,
	bool bipolar, int knobMinimum, int knobMaximum)
{
	parametersAsWritten = dynamicText.trimmed();
	editableValue = value;
	setObjectName(editorObjectName);
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	knob = new AudioKnob(this);
	knob->setObjectName(knobObjectName);
	knob->setBipolar(bipolar);
	knob->setRange(knobMinimum, knobMaximum);
	knob->setSingleStep(1);
	knob->setPageStep(10);
	layout->addWidget(knob, 0, Qt::AlignVCenter);

	QWidget* valueBlock = new QWidget(this);
	valueBlock->setObjectName(valueBlockObjectName);
	QVBoxLayout* valueLayout = new QVBoxLayout(valueBlock);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->setSpacing(6);
	if (caption != nullptr)
	{
		caption->setParent(valueBlock);
		valueLayout->addWidget(caption);
	}

	QWidget* displayedValue = editableValue;
	if (isDynamic())
	{
		QLabel* token = new QLabel(parametersAsWritten, valueBlock);
		token->setObjectName(QStringLiteral("DynamicValueToken"));
		token->setTextInteractionFlags(Qt::TextSelectableByMouse);
		QFont mono(token->font());
		mono.setFamily(SkinManager::instance()->tokens().monoFontFamily);
		token->setFont(mono);
		token->setToolTip(tr("Computed when the configuration loads; edit the raw line to change the expression."));
		displayedValue = token;
		knob->setEnabled(false);
		if (editableValue != nullptr)
			editableValue->deleteLater();
		editableValue = nullptr;
	}
	else if (editableValue != nullptr)
	{
		editableValue->setParent(valueBlock);
	}
	valueLayout->addWidget(displayedValue);

	layout->addWidget(valueBlock, 0, Qt::AlignVCenter);
	layout->addStretch(1);
}

void ScalarKnobCardEditor::synchronizeScalar(double value, int knobValue,
	const QString& knobText, bool notify)
{
	if (synchronizing || editableValue == nullptr)
		return;

	{
		const QScopedValueRollback<bool> sync(synchronizing, true);
		{
			const QSignalBlocker knobBlocker(knob);
			knob->setValue(knobValue);
		}
		knob->setValueText(knobText);
		{
			const QSignalBlocker valueBlocker(editableValue);
			editableValue->setValue(value);
		}
	}

	if (notify)
		emit updateModel();
}
