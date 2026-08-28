/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DelayCardEditor.h"

#include <cmath>

#include <QComboBox>
#include <QLocale>

#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"
#include "filters/DelayCommand.h"

namespace
{
// The legacy dial's logarithmic sweep (1..10000 over 1000 steps), kept so a
// knob turn covers the same musically useful range. Values typed outside it
// peg the knob at an end while the editable value keeps the real number,
// like the Preamp card.
constexpr double DialSteps = 1000.0;
constexpr double DialMin = 1.0;
constexpr double DialMax = 10000.0;
constexpr double MaximumDelay = 1000000.0;

int delayToKnobValue(double delay)
{
	if (delay <= DialMin)
		return 0;
	const int value = static_cast<int>(std::round(DialSteps * std::log(delay / DialMin) / std::log(DialMax / DialMin)));
	return qBound(0, value, static_cast<int>(DialSteps));
}

double knobValueToDelay(int value)
{
	return std::pow(DialMax / DialMin, value / DialSteps) * DialMin;
}
}

DelayCardEditor::DelayCardEditor(double delay, bool isMs, QWidget* parent)
	: ScalarKnobCardEditor(parent), msMode(isMs)
{
	EditableValue* editableValue = new EditableValue(this);
	editableValue->setObjectName(QStringLiteral("DelayCardValue"));
	editableValue->setUnit(msMode ? QStringLiteral("ms") : tr("samples"));
	editableValue->setDecimals(msMode ? 2 : 0);
	connect(editableValue, SIGNAL(valueChanged(double)), this, SLOT(valueChanged(double)));
	unitCombo = new QComboBox(this);
	unitCombo->setObjectName(QStringLiteral("DelayCardUnit"));
	unitCombo->setProperty("paramSelector", true);
	unitCombo->addItem(tr("Time"));
	unitCombo->addItem(tr("Samples"));
	unitCombo->setCurrentIndex(msMode ? 0 : 1);
	connect(unitCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(unitChanged(int)));
	initializeScalarCard(QStringLiteral("DelayCardEditor"), QStringLiteral("DelayCardKnob"),
		QStringLiteral("DelayCardValueBlock"), unitCombo, editableValue, QString(),
		false, 0, static_cast<int>(DialSteps));
	connect(scalarKnob(), SIGNAL(valueChanged(int)), this, SLOT(knobChanged(int)));

	setDelay(delay, false);
}

DelayCardEditor::DelayCardEditor(const QString& dynamicParameters, QWidget* parent)
	: ScalarKnobCardEditor(parent)
{
	unitCombo = new QComboBox(this);
	initializeScalarCard(QStringLiteral("DelayCardEditor"), QStringLiteral("DelayCardKnob"),
		QStringLiteral("DelayCardValueBlock"), unitCombo, nullptr, dynamicParameters,
		false, 0, static_cast<int>(DialSteps));
	// The unit lives inside the as-written text; a live selector would
	// promise a mode change the card cannot serialize.
	unitCombo->setVisible(false);
}

void DelayCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Delay");
	// Dynamic mode reproduces the expression verbatim; nothing emits
	// updateModel there, so this is belt and braces.
	if (isDynamic())
	{
		parameters = dynamicParameters();
		return;
	}

	// Serialize through the shared DelayCommand codec so the engine parser and
	// both Editor GUIs agree on one "<delay> ms|samples" format.
	DelayCommand cmd;
	cmd.delay = currentDelay;
	cmd.isMs = msMode;
	parameters = QString::fromStdWString(cmd.serialize());
}

void DelayCardEditor::knobChanged(int value)
{
	setDelay(knobValueToDelay(value), true);
}

void DelayCardEditor::valueChanged(double value)
{
	setDelay(value, true);
}

void DelayCardEditor::unitChanged(int index)
{
	if (isSynchronizing())
		return;

	msMode = index == 0;
	scalarValue()->setUnit(msMode ? QStringLiteral("ms") : tr("samples"));
	scalarValue()->setDecimals(msMode ? 2 : 0);
	// The number keeps its magnitude across the unit switch, exactly like the
	// legacy GUI: 5 ms becomes 5 samples as written.
	setDelay(currentDelay, true);
}

void DelayCardEditor::setDelay(double value, bool notify)
{
	currentDelay = qBound(0.0, value, MaximumDelay);
	// Samples are whole; the engine floors fractions anyway, so the editor is
	// honest about it up front.
	if (!msMode)
		currentDelay = std::round(currentDelay);

	synchronizeScalar(currentDelay, delayToKnobValue(currentDelay), delayText(), notify);
}

QString DelayCardEditor::delayText() const
{
	return QLocale::c().toString(currentDelay, 'f', msMode ? 2 : 0);
}

#include "FilterCardEditorRegistry.h"

#include "Editor/widgets/FilterCardModel.h"

REGISTER_DYNAMIC_FILTER_CARD_EDITOR(Delay, [](FilterTable*, const QString& command, const QString& parameters) -> IFilterGUI* {
	// An inline `expression` delay opens the dynamic card (token instead of
	// a number, knob powered down) - the parser below would reject the
	// unresolved text and drop the row to the raw body otherwise.
	if (FilterCardModel::hasInlineExpressions(parameters))
		return new DelayCardEditor(parameters);

	// Parse through the shared codec, not the engine factory: the factory
	// rejects a 0 delay so no run-time no-op filter is built, but the line
	// still owes the user its knob card - "Delay: 0 ms" used to collapse to
	// the collapsed raw body over exactly this.
	if (command != QStringLiteral("Delay"))
		return nullptr;
	DelayCommand cmd;
	if (!DelayCommand::parse(parameters.toStdWString(), cmd))
		return nullptr;
	return new DelayCardEditor(cmd.delay, cmd.isMs);
})
