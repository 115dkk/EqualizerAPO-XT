/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AllPassCardEditor.h"

#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Editor/analysis/AnalysisViewController.h"
#include "Editor/guis/BiQuadWidthConversion.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"
#include "Editor/widgets/SegmentedControl.h"

namespace
{
// The legacy dial sweeps, kept so a knob turn covers the same range it always
// has and a filter opened in either editor feels the same under the hand.
constexpr double DialSteps = 1000.0;
constexpr double FrequencyMin = 20.0;
constexpr double FrequencyMax = 20000.0;
constexpr double QMin = 0.3333;
constexpr double QMax = 33.3333;

int logKnobValue(double value, double minimum, double maximum)
{
	if (value <= minimum)
		return 0;
	const int step = static_cast<int>(std::round(
		DialSteps * std::log(value / minimum) / std::log(maximum / minimum)));
	return qBound(0, step, static_cast<int>(DialSteps));
}

double logKnobToValue(int step, double minimum, double maximum)
{
	return std::pow(maximum / minimum, step / DialSteps) * minimum;
}

QWidget* buildKnobBlock(QWidget* parent, AudioKnob*& knob, const QString& knobObjectName,
	QWidget* caption, EditableValue* value)
{
	// Named, because an unnamed QWidget is matched by whatever generic rule a
	// sheet carries and comes out as an opaque rectangle behind the chrome -
	// the speckling this card was reported for. Every container in a card body
	// has to be a name the skin sheets know and set transparent.
	QWidget* block = new QWidget(parent);
	block->setObjectName(QStringLiteral("AllPassCardParamBlock"));
	QHBoxLayout* layout = new QHBoxLayout(block);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	knob = new AudioKnob(block);
	knob->setObjectName(knobObjectName);
	knob->setRange(0, static_cast<int>(DialSteps));
	knob->setSingleStep(1);
	knob->setPageStep(10);
	layout->addWidget(knob, 0, Qt::AlignVCenter);

	QWidget* valueBlock = new QWidget(block);
	valueBlock->setObjectName(QStringLiteral("AllPassCardValueBlock"));
	QVBoxLayout* valueLayout = new QVBoxLayout(valueBlock);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->setSpacing(6);
	caption->setParent(valueBlock);
	valueLayout->addWidget(caption);
	value->setParent(valueBlock);
	valueLayout->addWidget(value);
	layout->addWidget(valueBlock, 0, Qt::AlignVCenter);

	return block;
}
}

AllPassCardEditor::AllPassCardEditor(const BiQuadCommand& command, const QString& commandName, QWidget* parent)
	: IFilterGUI(parent), originalCommand(commandName)
{
	setObjectName(QStringLiteral("AllPassCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(10);

	// What the filter does is said by the card's tooltip and, visibly, by the
	// fixed-magnitude readout at the row's tail. A standing sentence of prose
	// in the body was the first thing this card was told reads as legacy
	// chrome, and it is: no other card explains itself in a paragraph.
	setToolTip(tr("An all-pass changes phase and group delay around Fc. "
		"The magnitude response stays at 0 dB, so this filter is invisible in the magnitude graph."));

	// One row: the two knobs, then the order/graph switches and the magnitude
	// readout in the horizontal space right of them. Cards are wide, and a
	// second body row spent that width on nothing; the body sits in the card's
	// horizontal scroll wrapper, so a narrowed dock scrolls instead of
	// breaking.
	QHBoxLayout* parameterRow = new QHBoxLayout();
	parameterRow->setContentsMargins(0, 0, 0, 0);
	parameterRow->setSpacing(24);

	QLabel* frequencyCaption = new QLabel(tr("Center frequency"), this);
	frequencyCaption->setObjectName(QStringLiteral("AllPassCardCaption"));
	frequencyValue = new EditableValue(this);
	frequencyValue->setObjectName(QStringLiteral("AllPassCardFrequencyValue"));
	frequencyValue->setUnit(QStringLiteral("Hz"));
	frequencyValue->setDecimals(2);
	connect(frequencyValue, SIGNAL(valueChanged(double)), this, SLOT(frequencyValueChanged(double)));
	parameterRow->addWidget(buildKnobBlock(this, frequencyKnob,
		QStringLiteral("AllPassCardFrequencyKnob"), frequencyCaption, frequencyValue));
	connect(frequencyKnob, SIGNAL(valueChanged(int)), this, SLOT(frequencyKnobChanged(int)));

	// The width's caption is its mode selector, following the Delay card, whose
	// unit combo sits in exactly this position. It is a control, not a readout:
	// which spelling the line uses is the user's choice, and leaving it to
	// whatever the file happened to say is how the round-trip defect survived.
	widthModeCombo = new QComboBox(this);
	widthModeCombo->setObjectName(QStringLiteral("AllPassCardWidthMode"));
	widthModeCombo->setProperty("paramSelector", true);
	widthModeCombo->addItem(tr("Q factor"));
	widthModeCombo->addItem(tr("Bandwidth"));
	widthModeCombo->setCurrentIndex(command.isBandwidthOrS ? 1 : 0);
	widthValue = new EditableValue(this);
	widthValue->setObjectName(QStringLiteral("AllPassCardWidthValue"));
	widthValue->setDecimals(4);
	connect(widthValue, SIGNAL(valueChanged(double)), this, SLOT(widthValueChanged(double)));
	widthBlock = buildKnobBlock(this, widthKnob,
		QStringLiteral("AllPassCardWidthKnob"), widthModeCombo, widthValue);
	parameterRow->addWidget(widthBlock);
	connect(widthKnob, SIGNAL(valueChanged(int)), this, SLOT(widthKnobChanged(int)));
	connect(widthModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(widthModeChanged(int)));

	// Both mode switches follow the knobs on the same row, captioned alike, so
	// they read as a pair of settings rather than as one control stranded
	// beside the knobs.
	const auto captionedSegment = [this](const QString& blockName, const QString& segmentName,
		const QString& caption, const QStringList& labels, SegmentedControl*& out) {
		QWidget* block = new QWidget(this);
		block->setObjectName(blockName);
		QVBoxLayout* layout = new QVBoxLayout(block);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(6);
		QLabel* label = new QLabel(caption, block);
		label->setObjectName(QStringLiteral("AllPassCardCaption"));
		// Centred over the control it names. Left-aligned captions over compact
		// controls leave a ragged left edge across the row, which is what the
		// old footer was reported for; the knob blocks keep their left-aligned
		// captions because those sit over wide value fields and follow the
		// Preamp and Delay cards.
		label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		layout->addWidget(label);
		out = new SegmentedControl(block);
		out->setObjectName(segmentName);
		out->setLabels(labels);
		layout->addWidget(out);
		// The block is only as wide as its widest child, so centring the
		// caption centres it over the segment rather than over empty space.
		block->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
		return block;
	};

	QWidget* orderBlock = captionedSegment(QStringLiteral("AllPassCardOrderBlock"),
		QStringLiteral("AllPassCardOrderSegment"), tr("Order"), {tr("1st"), tr("2nd")}, orderSegment);
	orderSegment->setToolTip(tr("A 1st-order section turns 180 degrees in total and passes 90 degrees at Fc. "
		"A 2nd-order section turns a full 360 and has a width."));
	orderSegment->setCurrentIndex(command.type == BiQuad::ALL_PASS_1 ? 0 : 1);
	connect(orderSegment, &SegmentedControl::currentIndexChanged, this, &AllPassCardEditor::orderChanged);
	parameterRow->addWidget(orderBlock, 0, Qt::AlignVCenter);

	// One segment rather than two buttons: two buttons side by side would fill
	// the row with nothing but buttons, and these are two views of one thing,
	// not two independent actions.
	QWidget* graphBlock = captionedSegment(QStringLiteral("AllPassCardGraphBlock"),
		QStringLiteral("AllPassCardGraphSegment"), tr("Graph"), {tr("Phase"), tr("Group delay")}, graphSegment);
	graphSegment->setToolTip(tr("Show this reading in the analysis graph. The existing analysis is reused; nothing is measured again."));
	connect(graphSegment, &SegmentedControl::currentIndexChanged, this, [](int index) {
		AnalysisViewController::instance()->requestMetric(
			index == 1 ? AnalysisMetric::GroupDelayMs : AnalysisMetric::PhaseDegrees);
	});
	parameterRow->addWidget(graphBlock, 0, Qt::AlignVCenter);

	// The one reading this filter has that never moves, stated rather than left
	// to be inferred from a gain knob that is not there - the whole difficulty
	// with an all-pass is that its most obvious reading says nothing.
	//
	// It is a captioned readout in the same row as the two switches, not a
	// sentence pinned to the right margin. What marks it as a readout rather
	// than a value you can set is that it carries no field chrome: the two
	// values above sit in bordered boxes, this one is bare type. The mono font
	// comes from the active skin's tokens the way the dynamic value token in
	// ScalarKnobCardEditor takes it, because a static value has no sheet rule
	// of its own.
	QWidget* magnitudeBlock = new QWidget(this);
	magnitudeBlock->setObjectName(QStringLiteral("AllPassCardMagnitudeBlock"));
	QVBoxLayout* magnitudeLayout = new QVBoxLayout(magnitudeBlock);
	magnitudeLayout->setContentsMargins(0, 0, 0, 0);
	magnitudeLayout->setSpacing(6);
	QLabel* magnitudeCaption = new QLabel(tr("Magnitude"), magnitudeBlock);
	magnitudeCaption->setObjectName(QStringLiteral("AllPassCardCaption"));
	magnitudeCaption->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	magnitudeLayout->addWidget(magnitudeCaption);
	magnitudeNote = new QLabel(tr("0.0 dB"), magnitudeBlock);
	magnitudeNote->setObjectName(QStringLiteral("AllPassCardMagnitudeValue"));
	magnitudeNote->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	magnitudeNote->setToolTip(tr("An all-pass does not change level at any frequency, so there is nothing to set here."));
	magnitudeLayout->addWidget(magnitudeNote);
	magnitudeBlock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	parameterRow->addWidget(magnitudeBlock, 0, Qt::AlignVCenter);

	parameterRow->addStretch(1);
	mainLayout->addLayout(parameterRow);

	setFrequency(command.freq, false);
	// A line that arrived as 1st order carries no width, so the card starts
	// from the default rather than from zero - the value is kept live in the
	// model so switching to 2nd order and back does not lose it.
	setWidth(command.bandwidthOrQOrS != 0.0 ? command.bandwidthOrQOrS
		: BiQuadWidth::defaultQ(BiQuad::ALL_PASS), false);
	applyOrderVisibility();
}

bool AllPassCardEditor::firstOrder() const
{
	return orderSegment != nullptr && orderSegment->currentIndex() == 0;
}

void AllPassCardEditor::applyOrderVisibility()
{
	// Hidden, not disabled. A 1st-order section has no width at all, and a
	// control left greyed out in place says the opposite - that the value
	// exists and is merely unavailable.
	if (widthBlock != nullptr)
		widthBlock->setVisible(!firstOrder());
}

void AllPassCardEditor::orderChanged(int index)
{
	Q_UNUSED(index);
	if (synchronizing)
		return;
	applyOrderVisibility();
	emit updateModel();
}

bool AllPassCardEditor::bandwidthMode() const
{
	return widthModeCombo != nullptr && widthModeCombo->currentIndex() == 1;
}

void AllPassCardEditor::store(QString& command, QString& parameters)
{
	// The line keeps the command name it arrived with, number and all.
	command = originalCommand;
	if (firstOrder())
	{
		// No width is written, because the section does not have one. The card
		// keeps the value it would use at 2nd order, so switching back restores
		// it, but a value the filter ignores has no business in the file.
		parameters = QStringLiteral("ON AP Fc %1 Hz Order 1")
			.arg(QLocale::c().toString(currentFrequency, 'g', 10));
		return;
	}
	// The order is written out even though 2 is what an absent Order means.
	// A default hidden in the grammar cannot be changed later and cannot be
	// read off the file.
	parameters = QStringLiteral("ON AP Fc %1 Hz %2 %3 Order 2")
		.arg(QLocale::c().toString(currentFrequency, 'g', 10),
			bandwidthMode() ? QStringLiteral("BW Oct") : QStringLiteral("Q"),
			QLocale::c().toString(currentWidth, 'g', 10));
}

void AllPassCardEditor::frequencyKnobChanged(int value)
{
	setFrequency(logKnobToValue(value, FrequencyMin, FrequencyMax), true);
}

void AllPassCardEditor::frequencyValueChanged(double value)
{
	setFrequency(value, true);
}

void AllPassCardEditor::widthKnobChanged(int value)
{
	// The knob always sweeps Q, because that is the range a width knob has a
	// feel for. In bandwidth mode the swept Q is converted before it is shown,
	// so the knob covers the same filters either way round.
	const double q = logKnobToValue(value, QMin, QMax);
	setWidth(bandwidthMode() ? BiQuadWidth::bandwidthFromQ(q) : q, true);
}

void AllPassCardEditor::widthValueChanged(double value)
{
	setWidth(value, true);
}

void AllPassCardEditor::widthModeChanged(int index)
{
	if (synchronizing)
		return;

	// The user asked for the other spelling, so the number is converted to keep
	// the same width. This is the only path that converts: opening and saving
	// without touching this selector leaves the line's own words and its own
	// number exactly as they were.
	//
	// The conversion is exact between the two numbers but does not preserve the
	// filter's alpha, because the engine's bandwidth branch carries an
	// omega/sin(omega) factor the conversion does not. The difference is
	// nothing at low Fc and grows with it (about 0.3% at 1 kHz, 35% at 10 kHz).
	// Peaking has always behaved this way; correcting it would change that
	// filter's long-standing behaviour, which is a separate decision.
	const bool toBandwidth = index == 1;
	setWidth(toBandwidth ? BiQuadWidth::bandwidthFromQ(currentWidth)
		: BiQuadWidth::qFromBandwidth(currentWidth), true);
}

void AllPassCardEditor::setFrequency(double value, bool notify)
{
	currentFrequency = qBound(0.0, value, 1000000.0);
	if (synchronizing)
		return;

	{
		const QScopedValueRollback<bool> sync(synchronizing, true);
		{
			const QSignalBlocker blocker(frequencyKnob);
			frequencyKnob->setValue(logKnobValue(currentFrequency, FrequencyMin, FrequencyMax));
		}
		frequencyKnob->setValueText(QLocale::c().toString(currentFrequency, 'f', 0));
		{
			const QSignalBlocker valueBlocker(frequencyValue);
			frequencyValue->setValue(currentFrequency);
		}
	}

	if (notify)
		emit updateModel();
}

void AllPassCardEditor::setWidth(double value, bool notify)
{
	currentWidth = value;
	if (synchronizing)
		return;

	const double asQ = bandwidthMode() ? BiQuadWidth::qFromBandwidth(currentWidth) : currentWidth;
	{
		const QScopedValueRollback<bool> sync(synchronizing, true);
		{
			const QSignalBlocker blocker(widthKnob);
			widthKnob->setValue(logKnobValue(asQ, QMin, QMax));
		}
		widthKnob->setValueText(QLocale::c().toString(currentWidth, 'f', 3));
		{
			const QSignalBlocker blocker(widthValue);
			widthValue->setUnit(bandwidthMode() ? tr("Oct") : QString());
			widthValue->setValue(currentWidth);
		}
	}

	if (notify)
		emit updateModel();
}
