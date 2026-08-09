/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#include "Editor/skins/studio/cards/StudioSubwooferRoutingCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include "Editor/SkinManager.h"
#include "Editor/widgets/ElidedLabel.h"

namespace
{
void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}
}

StudioSubwooferRoutingCardView::StudioSubwooferRoutingCardView(
	QWidget* parent)
	: SubwooferRoutingCardView(parent)
{
	setObjectName(QStringLiteral("StudioSubwooferRoutingCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(14, 10, 14, 10);
	root->setSpacing(8);

	// Identity line: validity chip (lit glass chip grammar), the layout as
	// plain sans ink, and the profile receding to the right as muted ink.
	QHBoxLayout* identityRow = new QHBoxLayout();
	identityRow->setContentsMargins(0, 0, 0, 0);
	identityRow->setSpacing(10);

	validityChip = new QLabel(this);
	validityChip->setObjectName(QStringLiteral("StudioBassValidity"));
	validityChip->setAccessibleName(tr("Bass-management validity"));
	identityRow->addWidget(validityChip);

	layoutLabel = new QLabel(this);
	layoutLabel->setObjectName(QStringLiteral("StudioBassLayout"));
	layoutLabel->setAccessibleName(tr("Speaker layout"));
	layoutLabel->setToolTip(
		tr("Physical channel layout stored in this subwoofer-routing state"));
	identityRow->addWidget(layoutLabel);

	profileLabel = new ElidedLabel(this);
	profileLabel->setObjectName(QStringLiteral("StudioBassProfile"));
	profileLabel->setAccessibleName(tr("Bass-management profile"));
	identityRow->addWidget(profileLabel, 1);
	root->addLayout(identityRow);

	// The readout window: one sunken glass pane holding the three captioned
	// values. The crossover is the card's protagonist (brightest, largest
	// ink); source LFE and headroom follow at readout weight. The action
	// buttons sit outside the pane so the glass stays a data window.
	QHBoxLayout* readoutRow = new QHBoxLayout();
	readoutRow->setContentsMargins(0, 0, 0, 0);
	readoutRow->setSpacing(10);

	QWidget* readoutWell = new QWidget(this);
	readoutWell->setObjectName(QStringLiteral("StudioBassReadoutWell"));
	readoutWell->setAttribute(Qt::WA_StyledBackground, true);
	QHBoxLayout* wellLayout = new QHBoxLayout(readoutWell);
	wellLayout->setContentsMargins(14, 8, 14, 8);
	wellLayout->setSpacing(24);

	wellLayout->addWidget(makeReadoutCell(tr("CROSSOVER"),
		crossoverValue, true,
		tr("Representative crossover"),
		tr("Representative high-pass and low-pass crossover corner. "
			"Open the editor for the full per-group sections.")));
	wellLayout->addWidget(makeReadoutCell(tr("SOURCE LFE"),
		sourceLfeValue, false,
		tr("Source LFE routing"),
		tr("Whether the source LFE signal is preserved and at what gain")));
	wellLayout->addWidget(makeReadoutCell(tr("HEADROOM"),
		headroomValue, false,
		tr("Headroom"),
		tr("Automatic or manual headroom trim")));
	wellLayout->addStretch(1);

	readoutRow->addWidget(readoutWell, 1);

	QWidget* actionColumn = new QWidget(this);
	actionLayout = new QHBoxLayout(actionColumn);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	readoutRow->addWidget(actionColumn, 0, Qt::AlignVCenter);
	root->addLayout(readoutRow);

	// At most one quiet status line; the state contract guarantees that
	// errorText already covers invalid and missing-profile situations.
	statusLabel = new QLabel(this);
	statusLabel->setObjectName(QStringLiteral("StudioBassStatus"));
	statusLabel->setWordWrap(true);
	statusLabel->setVisible(false);
	statusLabel->setAccessibleName(tr("Bass-management status"));
	root->addWidget(statusLabel);
}

QWidget* StudioSubwooferRoutingCardView::makeReadoutCell(
	const QString& caption, QLabel*& valueLabel, bool primary,
	const QString& accessibleName, const QString& toolTip)
{
	QWidget* cell = new QWidget(this);
	cell->setObjectName(QStringLiteral("StudioBassReadoutCell"));
	QVBoxLayout* layout = new QVBoxLayout(cell);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(1);

	QLabel* captionLabel = new QLabel(caption, cell);
	captionLabel->setObjectName(
		QStringLiteral("StudioBassReadoutCaption"));
	captionLabel->setToolTip(toolTip);
	layout->addWidget(captionLabel);

	valueLabel = new QLabel(cell);
	valueLabel->setObjectName(primary
		? QStringLiteral("StudioBassReadoutValuePrimary")
		: QStringLiteral("StudioBassReadoutValue"));
	valueLabel->setAccessibleName(accessibleName);
	valueLabel->setToolTip(toolTip);
	layout->addWidget(valueLabel);
	return cell;
}

void StudioSubwooferRoutingCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setObjectName(QStringLiteral("StudioBassActionButton"));
	button->setCursor(Qt::PointingHandCursor);

	// Words, not pictograms (review round 3): the supplied 18px icons
	// rendered as unreadable specks on the glass chips, and even at a
	// legible size neither glyph names its action. The buttons already
	// carry real text - wear it.
	if (QToolButton* toolButton = qobject_cast<QToolButton*>(button))
		toolButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

	actionLayout->addWidget(button);
}

void StudioSubwooferRoutingCardView::applyState(
	const SubwooferRoutingCardState& state)
{
	const bool hasError = !state.errorText.isEmpty();
	const bool hasWarning = !state.warningText.isEmpty();

	if (hasError || !state.valid)
	{
		validityChip->setText(tr("ERROR"));
		validityChip->setProperty("severity",
			QStringLiteral("invalid"));
	}
	else if (hasWarning)
	{
		validityChip->setText(tr("WARNING"));
		validityChip->setProperty("severity",
			QStringLiteral("warning"));
	}
	else
	{
		validityChip->setText(tr("VALID"));
		validityChip->setProperty("severity",
			QStringLiteral("valid"));
	}
	repolish(validityChip);

	layoutLabel->setText(state.layoutLabel.isEmpty()
		? tr("Layout unknown")
		: tr("Layout %1").arg(state.layoutLabel));

	QString profileText;
	if (state.linkedProfile)
	{
		profileText = state.profileName.isEmpty()
			? tr("Linked profile")
			: state.profileName;
	}
	else if (!state.profileName.isEmpty())
	{
		profileText = state.profileName;
	}
	profileLabel->setFullText(profileText);
	profileLabel->setVisible(!profileText.isEmpty());

	crossoverValue->setText(crossoverSummary(state));

	sourceLfeValue->setText(state.sourceLfePreserved
		? tr("%1 dB").arg(
			QString::number(state.sourceLfeGainDb, 'f', 1))
		: tr("Off"));

	if (std::isfinite(state.headroomTrimDb))
	{
		headroomValue->setText(state.headroomAuto
			? tr("Auto %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1))
			: tr("Manual %1 dB").arg(
				QString::number(state.headroomTrimDb, 'f', 1)));
	}
	else
	{
		headroomValue->setText(state.headroomAuto
			? tr("Auto")
			: tr("Manual"));
	}

	// One line, error first - the contract forbids stacking synthesized
	// duplicates of the same fact.
	QString statusText;
	QString severity = QStringLiteral("normal");
	if (hasError)
	{
		statusText = state.errorText;
		severity = QStringLiteral("invalid");
	}
	else if (hasWarning)
	{
		statusText = state.warningText;
		severity = QStringLiteral("warning");
	}
	statusLabel->setText(statusText);
	statusLabel->setToolTip(statusText);
	statusLabel->setVisible(!statusText.isEmpty());
	statusLabel->setProperty("severity", severity);
	repolish(statusLabel);
}

void StudioSubwooferRoutingCardView::paintEvent(QPaintEvent* event)
{
	SubwooferRoutingCardView::paintEvent(event);

	if (!hasFocus())
		return;

	// The focus treatment is a light outline, not a shape outline: a thin
	// ring in the skin's focus colour on the card's single 8px radius.
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QColor ring(SkinManager::instance()->tokens().focusRing);
	ring.setAlpha(170);
	painter.setPen(QPen(ring, 1.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(
		QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
}
