#include "DelayCardEditor.h"

#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include "Editor/SkinManager.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"
#include "filters/DelayCommand.h"
#include "filters/DelayFilterFactory.h"

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
	: IFilterGUI(parent), msMode(isMs)
{
	editableValue = new EditableValue(this);
	editableValue->setObjectName(QStringLiteral("DelayCardValue"));
	editableValue->setUnit(msMode ? QStringLiteral("ms") : tr("samples"));
	editableValue->setDecimals(msMode ? 2 : 0);
	connect(editableValue, SIGNAL(valueChanged(double)), this, SLOT(valueChanged(double)));
	buildLayout(editableValue);

	setDelay(delay, false);
}

DelayCardEditor::DelayCardEditor(const QString& dynamicParameters, QWidget* parent)
	: IFilterGUI(parent), dynamicParameters(dynamicParameters.trimmed())
{
	QLabel* token = new QLabel(this->dynamicParameters, this);
	token->setObjectName(QStringLiteral("DynamicValueToken"));
	token->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont mono(token->font());
	mono.setFamily(SkinManager::instance()->tokens().monoFontFamily);
	token->setFont(mono);
	token->setToolTip(tr("Computed when the configuration loads; edit the raw line to change the expression."));
	buildLayout(token);

	knob->setEnabled(false);
	// The unit lives inside the as-written text; a live selector would
	// promise a mode change the card cannot serialize.
	unitCombo->setVisible(false);
}

void DelayCardEditor::buildLayout(QWidget* valueWidget)
{
	setObjectName(QStringLiteral("DelayCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	knob = new AudioKnob(this);
	knob->setObjectName(QStringLiteral("DelayCardKnob"));
	knob->setRange(0, static_cast<int>(DialSteps));
	knob->setSingleStep(1);
	knob->setPageStep(10);
	connect(knob, SIGNAL(valueChanged(int)), this, SLOT(knobChanged(int)));
	layout->addWidget(knob, 0, Qt::AlignVCenter);

	QWidget* valueBlock = new QWidget(this);
	valueBlock->setObjectName(QStringLiteral("DelayCardValueBlock"));
	QVBoxLayout* valueLayout = new QVBoxLayout(valueBlock);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->setSpacing(6);

	// The caption slot is the unit selector itself (the X5 grammar the BiQuad
	// parameter blocks established): a real mode choice, not a prose label
	// repeating the card title.
	unitCombo = new QComboBox(valueBlock);
	unitCombo->setObjectName(QStringLiteral("DelayCardUnit"));
	unitCombo->setProperty("paramSelector", true);
	unitCombo->addItem(tr("Time"));
	unitCombo->addItem(tr("Samples"));
	unitCombo->setCurrentIndex(msMode ? 0 : 1);
	connect(unitCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(unitChanged(int)));
	valueLayout->addWidget(unitCombo);

	valueWidget->setParent(valueBlock);
	valueLayout->addWidget(valueWidget);

	layout->addWidget(valueBlock, 0, Qt::AlignVCenter);
	layout->addStretch(1);
}

void DelayCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Delay");
	// Dynamic mode reproduces the expression verbatim; nothing emits
	// updateModel there, so this is belt and braces.
	if (!dynamicParameters.isEmpty())
	{
		parameters = dynamicParameters;
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
	if (updating)
		return;

	msMode = index == 0;
	editableValue->setUnit(msMode ? QStringLiteral("ms") : tr("samples"));
	editableValue->setDecimals(msMode ? 2 : 0);
	// The number keeps its magnitude across the unit switch, exactly like the
	// legacy GUI: 5 ms becomes 5 samples as written.
	setDelay(currentDelay, true);
}

void DelayCardEditor::setDelay(double value, bool notify)
{
	if (updating)
		return;

	updating = true;
	currentDelay = qBound(0.0, value, MaximumDelay);
	// Samples are whole; the engine floors fractions anyway, so the editor is
	// honest about it up front.
	if (!msMode)
		currentDelay = std::round(currentDelay);

	const bool knobBlocked = knob->blockSignals(true);
	knob->setValue(delayToKnobValue(currentDelay));
	knob->blockSignals(knobBlocked);
	knob->setValueText(delayText());

	const bool valueBlocked = editableValue->blockSignals(true);
	editableValue->setValue(currentDelay);
	editableValue->blockSignals(valueBlocked);
	updating = false;

	if (notify)
		emit updateModel();
}

QString DelayCardEditor::delayText() const
{
	return QLocale::c().toString(currentDelay, 'f', msMode ? 2 : 0);
}

#include "FilterCardEditorRegistry.h"

#include "Editor/widgets/FilterCardModel.h"

REGISTER_FILTER_CARD_EDITOR(delay, [](FilterTable*, const QString& command, const QString& parameters) -> IFilterGUI* {
	// An inline `expression` delay opens the dynamic card (token instead of
	// a number, knob powered down) - the engine parser below would reject the
	// unresolved text and drop the row to the raw body otherwise.
	if (FilterCardModel::hasInlineExpressions(parameters))
		return new DelayCardEditor(parameters);

	// Parse through the engine's shared routine; a line it rejects falls back
	// to the legacy factory chain, exactly like the legacy GUI factory.
	DelayCommand cmd;
	std::wstring wideCommand = command.toStdWString();
	std::wstring wideParameters = parameters.toStdWString();
	if (!DelayFilterFactory::parseCommand(wideCommand, wideParameters, cmd))
		return nullptr;
	return new DelayCardEditor(cmd.delay, cmd.isMs);
})
