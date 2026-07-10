#include "PreampCardEditor.h"

#include <cmath>

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QRegularExpression>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/EditableValue.h"

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
	: IFilterGUI(parent)
{
	editableValue = new EditableValue(this);
	editableValue->setObjectName(QStringLiteral("PreampCardValue"));
	editableValue->setUnit(QStringLiteral("dB"));
	connect(editableValue, SIGNAL(valueChanged(double)), this, SLOT(valueChanged(double)));
	buildLayout(editableValue);

	setGain(dbGain, false);
}

PreampCardEditor::PreampCardEditor(const QString& dynamicParameters, QWidget* parent)
	: IFilterGUI(parent), dynamicParameters(dynamicParameters.trimmed())
{
	// The value position carries the expression as written; the knob stays as
	// hardware but is powered down (there is no number to point at until the
	// engine loads the configuration).
	QLabel* token = new QLabel(this->dynamicParameters, this);
	token->setObjectName(QStringLiteral("DynamicValueToken"));
	token->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont mono(token->font());
	mono.setFamily(SkinManager::instance()->tokens().monoFontFamily);
	token->setFont(mono);
	token->setToolTip(tr("Computed when the configuration loads; edit the raw line to change the expression."));
	buildLayout(token);

	knob->setEnabled(false);
}

void PreampCardEditor::buildLayout(QWidget* valueWidget)
{
	setObjectName(QStringLiteral("PreampCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(14);

	knob = new AudioKnob(this);
	knob->setObjectName(QStringLiteral("PreampCardKnob"));
	knob->setBipolar(true);
	// A gain outside the knob span (typed directly) pegs the knob at its end
	// while the editable value keeps showing the real number.
	const double knobRange = GUIHelper::knobGainRange();
	knob->setRange(gainToKnobValue(-knobRange), gainToKnobValue(knobRange));
	knob->setSingleStep(1);
	knob->setPageStep(10);
	connect(knob, SIGNAL(valueChanged(int)), this, SLOT(knobChanged(int)));
	layout->addWidget(knob, 0, Qt::AlignVCenter);

	QWidget* valueBlock = new QWidget(this);
	valueBlock->setObjectName(QStringLiteral("PreampCardValueBlock"));
	QVBoxLayout* valueLayout = new QVBoxLayout(valueBlock);
	valueLayout->setContentsMargins(0, 0, 0, 0);
	valueLayout->setSpacing(6);

	QLabel* caption = new QLabel(tr("Gain"), valueBlock);
	caption->setObjectName(QStringLiteral("PreampCardCaption"));
	valueLayout->addWidget(caption);

	valueWidget->setParent(valueBlock);
	valueLayout->addWidget(valueWidget);

	layout->addWidget(valueBlock, 0, Qt::AlignVCenter);
	layout->addStretch(1);
}

void PreampCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Preamp");
	// Dynamic mode reproduces the expression verbatim - the card never
	// writes a computed number over it. (Nothing emits updateModel in that
	// mode, so this is belt and braces.)
	if (!dynamicParameters.isEmpty())
	{
		parameters = dynamicParameters;
		return;
	}
	parameters = QStringLiteral("%0 dB").arg(QLocale::c().toString(currentGain, 'f', 1));
}

double PreampCardEditor::parseGain(const QString& parameters)
{
	static const QRegularExpression numberExpression(QStringLiteral("([-+]?\\d+(?:\\.\\d+)?)"));
	const QRegularExpressionMatch match = numberExpression.match(parameters);
	if (!match.hasMatch())
		return 0.0;

	bool ok = false;
	const double value = QLocale::c().toDouble(match.captured(1), &ok);
	return ok ? value : 0.0;
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
	if (updating)
		return;

	updating = true;
	currentGain = qBound(MinimumGain, value, MaximumGain);
	const int knobValue = gainToKnobValue(currentGain);
	const bool knobBlocked = knob->blockSignals(true);
	knob->setValue(knobValue);
	knob->blockSignals(knobBlocked);
	knob->setValueText(gainText());

	const bool valueBlocked = editableValue->blockSignals(true);
	editableValue->setValue(currentGain);
	editableValue->blockSignals(valueBlocked);
	updating = false;

	if (notify)
		emit updateModel();
}

QString PreampCardEditor::gainText() const
{
	return QLocale::c().toString(currentGain, 'f', 1);
}

#include "FilterCardEditorRegistry.h"

#include "Editor/widgets/FilterCardModel.h"

REGISTER_FILTER_CARD_EDITOR(preamp, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	// An inline `expression` gain opens the dynamic card (token instead of a
	// number, knob powered down) so no interaction can overwrite the
	// expression with a parsed 0.0.
	if (FilterCardModel::hasInlineExpressions(parameters))
		return new PreampCardEditor(parameters);
	return new PreampCardEditor(PreampCardEditor::parseGain(parameters));
})
