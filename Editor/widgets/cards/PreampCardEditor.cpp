#include "PreampCardEditor.h"

#include <cmath>

#include <QLabel>
#include <QLocale>
#include <QRegularExpression>

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

REGISTER_DYNAMIC_FILTER_CARD_EDITOR(preamp, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
	// An inline `expression` gain opens the dynamic card (token instead of a
	// number, knob powered down) so no interaction can overwrite the
	// expression with a parsed 0.0.
	if (FilterCardModel::hasInlineExpressions(parameters))
		return new PreampCardEditor(parameters);
	return new PreampCardEditor(PreampCardEditor::parseGain(parameters));
})
