#include "AllPassCardEditor.h"

#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
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
	QWidget* block = new QWidget(parent);
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

	QLabel* description = new QLabel(
		tr("Changes phase and group delay around Fc. The magnitude response stays at 0 dB."), this);
	description->setObjectName(QStringLiteral("AllPassCardDescription"));
	description->setWordWrap(true);
	mainLayout->addWidget(description);

	// Two knobs side by side, the way the legacy row already places them. The
	// card deliberately does not grow a third or fourth column for the readout
	// and the buttons: the card body has no horizontal minimum today, and
	// adding columns would give it one, so a narrowed dock would break this
	// card before any other.
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
	parameterRow->addWidget(buildKnobBlock(this, widthKnob,
		QStringLiteral("AllPassCardWidthKnob"), widthModeCombo, widthValue));
	connect(widthKnob, SIGNAL(valueChanged(int)), this, SLOT(widthKnobChanged(int)));
	connect(widthModeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(widthModeChanged(int)));

	parameterRow->addStretch(1);
	mainLayout->addLayout(parameterRow);

	QHBoxLayout* footerRow = new QHBoxLayout();
	footerRow->setContentsMargins(0, 0, 0, 0);
	footerRow->setSpacing(12);

	// One segment rather than two buttons: two buttons side by side would fill
	// the row with nothing but buttons, and these are two views of one thing,
	// not two independent actions.
	graphSegment = new SegmentedControl(this);
	graphSegment->setObjectName(QStringLiteral("AllPassCardGraphSegment"));
	graphSegment->setLabels({tr("Phase"), tr("Group delay")});
	graphSegment->setToolTip(tr("Show this reading in the analysis graph. The existing analysis is reused; nothing is measured again."));
	connect(graphSegment, &SegmentedControl::currentIndexChanged, this, [](int index) {
		AnalysisViewController::instance()->requestMetric(
			index == 1 ? AnalysisMetric::GroupDelayMs : AnalysisMetric::PhaseDegrees);
	});
	footerRow->addWidget(graphSegment, 0, Qt::AlignVCenter);
	footerRow->addStretch(1);

	// Stated rather than left to be inferred from a knob that is not there. The
	// whole difficulty with this filter is that its most obvious reading says
	// nothing.
	magnitudeNote = new QLabel(tr("Magnitude: 0.0 dB (fixed)"), this);
	magnitudeNote->setObjectName(QStringLiteral("AllPassCardMagnitudeNote"));
	footerRow->addWidget(magnitudeNote, 0, Qt::AlignVCenter);
	mainLayout->addLayout(footerRow);

	setFrequency(command.freq, false);
	setWidth(command.bandwidthOrQOrS, false);
}

bool AllPassCardEditor::bandwidthMode() const
{
	return widthModeCombo != nullptr && widthModeCombo->currentIndex() == 1;
}

void AllPassCardEditor::store(QString& command, QString& parameters)
{
	// The line keeps the command name it arrived with, number and all.
	command = originalCommand;
	parameters = QStringLiteral("ON AP Fc %1 Hz %2 %3")
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

	synchronizing = true;
	{
		const QSignalBlocker blocker(frequencyKnob);
		frequencyKnob->setValue(logKnobValue(currentFrequency, FrequencyMin, FrequencyMax));
	}
	frequencyKnob->setValueText(QLocale::c().toString(currentFrequency, 'f', 0));
	{
		const QSignalBlocker blocker(frequencyValue);
		frequencyValue->setValue(currentFrequency);
	}
	synchronizing = false;

	if (notify)
		emit updateModel();
}

void AllPassCardEditor::setWidth(double value, bool notify)
{
	currentWidth = value;
	if (synchronizing)
		return;

	const double asQ = bandwidthMode() ? BiQuadWidth::qFromBandwidth(currentWidth) : currentWidth;
	synchronizing = true;
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
	synchronizing = false;

	if (notify)
		emit updateModel();
}
