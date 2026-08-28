/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PreampCardEditor.h"

#include <cmath>

#include <QLabel>
#include <QLocale>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"
#include "filters/PreampCommand.h"
#include "filters/PreampFilterFactory.h"

namespace
{
// Bounds for directly typed values; the knob itself only spans the
// user-configured GUIHelper::knobGainRange() so a small turn stays a small
// change.
constexpr double MinimumGain = -100.0;
constexpr double MaximumGain = 100.0;
constexpr double GainStep = 0.1;

int gainToKnobValue(double gain)
{
	return static_cast<int>(std::round(gain / GainStep));
}

double knobValueToGain(int value)
{
	return value * GainStep;
}
}

PreampCardEditor::PreampCardEditor(double dbGain, QWidget* parent)
	: ScalarKnobCardEditor(parent)
{
	EditableValue* editableValue = new EditableValue(this);
	editableValue->setObjectName(QStringLiteral("PreampCardValue"));
	editableValue->setUnit(QStringLiteral("dB"));
	connect(editableValue, SIGNAL(valueChanged(double)), this, SLOT(valueChanged(double)));
	QLabel* caption = new QLabel(tr("Gain"), this);
	caption->setObjectName(QStringLiteral("PreampCardCaption"));
	const double knobRange = GUIHelper::knobGainRange();
	initializeScalarCard(QStringLiteral("PreampCardEditor"), QStringLiteral("PreampCardKnob"),
		QStringLiteral("PreampCardValueBlock"), caption, editableValue, QString(), true,
		gainToKnobValue(-knobRange), gainToKnobValue(knobRange));
	connect(scalarKnob(), SIGNAL(valueChanged(int)), this, SLOT(knobChanged(int)));

	setGain(dbGain, false);
}

PreampCardEditor::PreampCardEditor(const QString& dynamicParameters, QWidget* parent)
	: ScalarKnobCardEditor(parent)
{
	QLabel* caption = new QLabel(tr("Gain"), this);
	caption->setObjectName(QStringLiteral("PreampCardCaption"));
	const double knobRange = GUIHelper::knobGainRange();
	initializeScalarCard(QStringLiteral("PreampCardEditor"), QStringLiteral("PreampCardKnob"),
		QStringLiteral("PreampCardValueBlock"), caption, nullptr, dynamicParameters, true,
		gainToKnobValue(-knobRange), gainToKnobValue(knobRange));
}

void PreampCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Preamp");
	// Dynamic mode reproduces the expression verbatim - the card never
	// writes a computed number over it. (Nothing emits updateModel in that
	// mode, so this is belt and braces.)
	if (isDynamic())
	{
		parameters = dynamicParameters();
		return;
	}

	// Serialize through the shared PreampCommand codec, like the legacy GUI and
	// the Delay card do, so one line cannot come back in two different formats
	// depending on which editor the user happened to have open. The card used to
	// write a fixed single decimal here, which silently rounded away anything
	// finer (a -6.25 dB line became -6.2 dB on the first knob touch).
	PreampCommand cmd;
	cmd.dbGain = currentGain;
	cmd.valid = true;
	cmd.noOp = std::abs(currentGain) < 1e-9;
	parameters = QString::fromStdWString(cmd.serialize());
}

void PreampCardEditor::knobChanged(int value)
{
	setGain(knobValueToGain(value), true);
}

void PreampCardEditor::valueChanged(double value)
{
	setGain(value, true);
}

void PreampCardEditor::setGain(double value, bool notify)
{
	currentGain = qBound(MinimumGain, value, MaximumGain);
	const int knobValue = gainToKnobValue(currentGain);
	synchronizeScalar(currentGain, knobValue, gainText(), notify);
}

QString PreampCardEditor::gainText() const
{
	return QLocale::c().toString(currentGain, 'f', 1);
}

#include "FilterCardEditorRegistry.h"

#include "Editor/widgets/FilterCardModel.h"

REGISTER_DYNAMIC_FILTER_CARD_EDITOR(Preamp, [](FilterTable*, const QString& command, const QString& parameters) -> IFilterGUI* {
	// An inline `expression` gain opens the dynamic card (token instead of a
	// number, knob powered down) so no interaction can overwrite the
	// expression with a parsed 0.0.
	if (FilterCardModel::hasInlineExpressions(parameters))
		return new PreampCardEditor(parameters);

	// Read the number through the engine's own parse routine. The card's former
	// regex took only the first "-?digits(.digits)?" run, so it read "-6,5 dB"
	// as -6 and "1e1 dB" as 1 while the engine read -6.5 and 10; one knob touch
	// then wrote the card's misreading back over the line. A malformed value
	// yields no card, matching the legacy GUI factory: the line stays raw text
	// instead of silently becoming 0 dB.
	std::wstring wideCommand = command.toStdWString();
	std::wstring wideParameters = parameters.toStdWString();
	PreampCommand cmd;
	if (!PreampFilterFactory::parseCommand(wideCommand, wideParameters, cmd) || !cmd.valid)
		return nullptr;
	return new PreampCardEditor(cmd.dbGain);
})
