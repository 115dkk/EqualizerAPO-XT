/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's subwoofer-routing card: a departure-board posting (see the
	header). Boxed sunken mono cells post the facts, the state cell spends
	the only traffic-light colour, and faults arrive as one remark line.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#include "MatrixSubwooferRoutingCardView.h"

#include <QAbstractButton>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/ElidedLabel.h"

MatrixSubwooferRoutingCardView::MatrixSubwooferRoutingCardView(
	const SkinTokens& tokens, QWidget* parent)
	: SubwooferRoutingCardView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("MatrixSubwooferRoutingCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	// The posting line: captioned boxed cells on the board grid. Reading
	// order follows the user's questions - is it healthy, what layout,
	// where is the crossover, what happens to LFE, how much trim, whose
	// profile.
	QHBoxLayout* postingRow = new QHBoxLayout();
	postingRow->setContentsMargins(0, 0, 0, 0);
	postingRow->setSpacing(12);

	postingRow->addWidget(makeReadoutColumn(tr("STATE"), stateCell,
		tr("Bass-management validity"),
		tr("Whether this subwoofer-routing state passes validation")));
	postingRow->addWidget(makeReadoutColumn(tr("LAYOUT"), layoutCell,
		tr("Speaker layout"),
		tr("Physical channel layout stored in this state")));
	postingRow->addWidget(makeReadoutColumn(tr("XOVER"), crossoverCell,
		tr("Representative crossover"),
		tr("Representative high-pass and low-pass crossover corner; "
			"the full editor posts the per-group sections")));
	postingRow->addWidget(makeReadoutColumn(tr("LFE"), lfeCell,
		tr("Source LFE routing"),
		tr("Whether the source LFE signal is preserved and at what gain")));
	postingRow->addWidget(makeReadoutColumn(tr("TRIM"), trimCell,
		tr("Headroom trim"),
		tr("Automatic or manual headroom trim")));

	// The profile is the one cell whose payload is unbounded (a file name),
	// so it takes the leftover width and elides instead of pushing the
	// posting line past the viewport.
	{
		QWidget* column = new QWidget(this);
		column->setObjectName(
			QStringLiteral("MatrixBassReadoutColumn"));
		QVBoxLayout* layout = new QVBoxLayout(column);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(2);

		QLabel* captionLabel = new QLabel(tr("PROFILE"), column);
		captionLabel->setObjectName(
			QStringLiteral("MatrixBassCaption"));
		layout->addWidget(captionLabel);

		ElidedLabel* cell = new ElidedLabel(column);
		cell->setObjectName(QStringLiteral("MatrixBassValueCell"));
		cell->setAccessibleName(tr("Bass-management profile"));
		cell->setToolTip(
			tr("Embedded state or linked profile name"));
		layout->addWidget(cell);
		profileCell = cell;
		postingRow->addWidget(column);
	}

	QWidget* actionColumn = new QWidget(this);
	actionLayout = new QHBoxLayout(actionColumn);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	postingRow->addWidget(actionColumn, 0, Qt::AlignBottom);
	postingRow->addStretch(1);
	root->addLayout(postingRow);

	// One remark line for faults; the contract guarantees errorText already
	// covers invalid states and missing linked profiles, so nothing here
	// re-posts the same fact.
	remarkLine = new QLabel(this);
	remarkLine->setObjectName(QStringLiteral("MatrixBassRemark"));
	remarkLine->setWordWrap(true);
	remarkLine->setVisible(false);
	remarkLine->setAccessibleName(tr("Bass-management status"));
	root->addWidget(remarkLine);
}

QWidget* MatrixSubwooferRoutingCardView::makeReadoutColumn(
	const QString& caption, QLabel*& valueCell,
	const QString& accessibleName, const QString& toolTip)
{
	QWidget* column = new QWidget(this);
	column->setObjectName(QStringLiteral("MatrixBassReadoutColumn"));
	QVBoxLayout* layout = new QVBoxLayout(column);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);

	QLabel* captionLabel = new QLabel(caption, column);
	captionLabel->setObjectName(QStringLiteral("MatrixBassCaption"));
	captionLabel->setToolTip(toolTip);
	layout->addWidget(captionLabel);

	valueCell = new QLabel(column);
	valueCell->setObjectName(QStringLiteral("MatrixBassValueCell"));
	valueCell->setAccessibleName(accessibleName);
	valueCell->setToolTip(toolTip);
	layout->addWidget(valueCell);
	return column;
}

void MatrixSubwooferRoutingCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setObjectName(QStringLiteral("MatrixBassActionButton"));
	button->setCursor(Qt::PointingHandCursor);
	actionLayout->addWidget(button);
}

void MatrixSubwooferRoutingCardView::applyState(
	const SubwooferRoutingCardState& state)
{
	const Severity health = severity(state);

	// The one place the ration book allows colour: green = in service,
	// amber = caution, red = fault.
	if (health == Severity::Error)
	{
		stateCell->setText(tr("FAULT"));
		stateCell->setProperty("tone", QStringLiteral("danger"));
	}
	else if (health == Severity::Warning)
	{
		stateCell->setText(tr("CHECK"));
		stateCell->setProperty("tone", QStringLiteral("warning"));
	}
	else
	{
		stateCell->setText(tr("OK"));
		stateCell->setProperty("tone", QStringLiteral("ok"));
	}
	repolish(stateCell);

	layoutCell->setText(state.layoutLabel.isEmpty()
		? QStringLiteral("--")
		: state.layoutLabel);

	crossoverCell->setText(
		state.highPassHz > 0.0 || state.lowPassHz > 0.0
			? crossoverSummary(state)
			: QStringLiteral("--"));

	lfeCell->setText(state.sourceLfePreserved
		? tr("%1 dB").arg(
			QString::number(state.sourceLfeGainDb, 'f', 1))
		: tr("OFF"));

	trimCell->setText(headroomText(state,
		tr("AUTO %1 dB"), tr("%1 dB"), tr("AUTO"), QStringLiteral("--")));

	profileCell->setFullText(profileText(state, tr("LINKED"), tr("EMBEDDED")));

	// One remark, highest severity first - a board posts a fault once.
	QString remark;
	QString tone = QStringLiteral("normal");
	if (health == Severity::Error)
	{
		remark = tr("! FAULT: %1").arg(state.errorText.isEmpty()
			? tr("State is invalid")
			: state.errorText);
		tone = QStringLiteral("danger");
	}
	else if (health == Severity::Warning)
	{
		remark = tr("! CHECK: %1").arg(state.warningText);
		tone = QStringLiteral("warning");
	}
	remarkLine->setText(remark);
	remarkLine->setToolTip(remark);
	remarkLine->setVisible(!remark.isEmpty());
	remarkLine->setProperty("tone", tone);
	repolish(remarkLine);
}
