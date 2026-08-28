/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "IIRCardEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FlowLayout.h"
#include "Editor/widgets/ValueScrubBox.h"
#include "filters/IIRCommand.h"
#include "filters/IIRFilterFactory.h"

namespace
{
// The engine puts no ceiling on the order, but the card form does: 16 covers
// every filter a design tool exports in practice while keeping the two
// coefficient rows a readable size. Longer lines still load - the order box
// only clamps values typed or scrubbed in the card.
constexpr int MinimumOrder = 1;
constexpr int MaximumOrder = 16;

// Coefficients are dimensionless and usually below 1 in magnitude; the range
// is only a sanity bound for typed values. Six decimals match the "%g"
// precision IIRCommand::serialize writes.
constexpr double CoefficientLimit = 1000000.0;
constexpr int CoefficientDecimals = 6;
}

IIRCardEditor::IIRCardEditor(unsigned order, const std::vector<double>& coefficients, QWidget* parent)
	: IFilterGUI(parent), order(order)
{
	setObjectName(QStringLiteral("IIRCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	// parseCommand guarantees (order + 1) * 2 values: b0..bN, then a0..aN.
	const std::vector<double>::const_iterator split = coefficients.begin() + static_cast<int>(order) + 1;
	feedforward.assign(coefficients.begin(), split);
	feedback.assign(split, coefficients.end());

	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(8);

	// The order rides the Preamp value-block grammar (caption over value); the
	// value itself is a scrub field because a whole-number order has no
	// musically useful knob sweep.
	QHBoxLayout* orderLayout = new QHBoxLayout();
	orderLayout->setContentsMargins(0, 0, 0, 0);
	orderLayout->setSpacing(14);

	QWidget* orderBlock = new QWidget(this);
	orderBlock->setObjectName(QStringLiteral("IIRCardOrderBlock"));
	QVBoxLayout* orderBlockLayout = new QVBoxLayout(orderBlock);
	orderBlockLayout->setContentsMargins(0, 0, 0, 0);
	orderBlockLayout->setSpacing(6);

	QLabel* orderCaption = new QLabel(tr("Order"), orderBlock);
	orderCaption->setObjectName(QStringLiteral("IIRCardCaption"));
	orderBlockLayout->addWidget(orderCaption);

	orderBox = new ValueScrubIntBox(orderBlock);
	orderBox->setObjectName(QStringLiteral("IIRCardOrderBox"));
	orderBox->setRange(MinimumOrder, MaximumOrder);
	orderBox->setKeyboardTracking(false);
	orderBox->setValue(static_cast<int>(order));
	connect(orderBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &IIRCardEditor::orderChanged);
	orderBlockLayout->addWidget(orderBox);

	orderLayout->addWidget(orderBlock, 0, Qt::AlignVCenter);
	orderLayout->addStretch(1);
	mainLayout->addLayout(orderLayout);

	// The two coefficient vectors as labeled rows. The row captions are real
	// prose (translated); the per-field b0/a0 captions are the math symbols as
	// the config line spells them, so they stay untranslated data captions.
	QLabel* feedforwardCaption = new QLabel(tr("Feedforward (b)"), this);
	feedforwardCaption->setObjectName(QStringLiteral("IIRCardCaption"));
	mainLayout->addWidget(feedforwardCaption);

	feedforwardFlow = new FlowLayout(0, 6, 6);
	mainLayout->addLayout(feedforwardFlow);

	QLabel* feedbackCaption = new QLabel(tr("Feedback (a)"), this);
	feedbackCaption->setObjectName(QStringLiteral("IIRCardCaption"));
	mainLayout->addWidget(feedbackCaption);

	feedbackFlow = new FlowLayout(0, 6, 6);
	mainLayout->addLayout(feedbackFlow);

	rebuildRows();
}

void IIRCardEditor::store(QString& command, QString& parameters)
{
	// Serialize through the shared IIRCommand codec so the engine parser and
	// the card agree on one "ON IIR Order N Coefficients ..." format.
	IIRCommand cmd;
	cmd.order = order;
	cmd.coefficients = feedforward;
	cmd.coefficients.insert(cmd.coefficients.end(), feedback.begin(), feedback.end());

	command = QStringLiteral("Filter");
	parameters = QString::fromStdWString(cmd.serialize());
}

void IIRCardEditor::orderChanged(int value)
{
	if (updating)
		return;

	order = static_cast<unsigned>(value);
	// Growing the order appends zero taps (audibly a no-op until edited);
	// shrinking truncates. a0 always survives because it is the front of its
	// half, so the line stays runnable across a round trip.
	feedforward.resize(order + 1, 0.0);
	feedback.resize(order + 1, 0.0);
	// Safe to rebuild: the sender is the persistent order box, never one of
	// the coefficient fields the rebuild deletes (the Device card's rule).
	rebuildRows();
	emit updateModel();
}

void IIRCardEditor::coefficientChanged()
{
	if (updating)
		return;

	for (int i = 0; i < feedforwardBoxes.size(); i++)
		feedforward[size_t(i)] = feedforwardBoxes[i]->value();
	for (int i = 0; i < feedbackBoxes.size(); i++)
		feedback[size_t(i)] = feedbackBoxes[i]->value();
	emit updateModel();
}

void IIRCardEditor::rebuildRows()
{
	updating = true;
	rebuildRow(feedforwardFlow, feedforwardBoxes, feedforward, QLatin1Char('b'));
	rebuildRow(feedbackFlow, feedbackBoxes, feedback, QLatin1Char('a'));
	updating = false;
}

void IIRCardEditor::rebuildRow(FlowLayout* flow, QVector<ValueScrubBox*>& boxes, const std::vector<double>& values, QChar prefix)
{
	while (flow->count() > 0)
	{
		QLayoutItem* child = flow->takeAt(flow->count() - 1);
		delete child->widget();
		delete child;
	}
	boxes.clear();

	for (size_t i = 0; i < values.size(); i++)
	{
		QWidget* block = new QWidget(this);
		block->setObjectName(QStringLiteral("IIRCardCoefficientBlock"));
		QVBoxLayout* blockLayout = new QVBoxLayout(block);
		blockLayout->setContentsMargins(0, 0, 0, 0);
		blockLayout->setSpacing(2);

		QLabel* caption = new QLabel(QStringLiteral("%1%2").arg(prefix).arg(int(i)), block);
		caption->setObjectName(QStringLiteral("IIRCardCoefficientCaption"));
		blockLayout->addWidget(caption);

		ValueScrubBox* box = new ValueScrubBox(block);
		box->setObjectName(QStringLiteral("IIRCardCoefficientBox"));
		box->setDecimals(CoefficientDecimals);
		box->setRange(-CoefficientLimit, CoefficientLimit);
		box->setSingleStep(0.01);
		box->setKeyboardTracking(false);
		// A fixed width keeps the flow rows a regular grid; long typed values
		// scroll inside the field instead of stretching it.
		box->setFixedWidth(GUIHelper::scale(96));
		box->setValue(values[i]);
		connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &IIRCardEditor::coefficientChanged);
		blockLayout->addWidget(box);

		flow->addWidget(block);
		boxes.append(box);
	}
}

// The "Filter" registration lives in FilterCardEditorRouter.cpp: IIR is not
// the only card behind that keyword, and the registry holds one creator per
// key, so the choice between IIR, all-pass and the legacy GUI has to be made
// in one place rather than raced between translation units.
