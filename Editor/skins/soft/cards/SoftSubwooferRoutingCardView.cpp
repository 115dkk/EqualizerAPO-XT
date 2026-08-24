/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#include "SoftSubwooferRoutingCardView.h"

#include <cmath>

#include <QAbstractButton>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
void repolish(QWidget* widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

QString decibels(double value)
{
	return QString::number(value, 'f', 1);
}
}

SoftSubwooferRoutingCardView::SoftSubwooferRoutingCardView(
	const SkinTokens& tokens, QWidget* parent)
	: SubwooferRoutingCardView(parent),
	  skinTokens(tokens)
{
	setObjectName(QStringLiteral("SoftSubwooferRoutingCardView"));

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(16, 12, 16, 12);
	root->setSpacing(6);

	// Headline sentence + the action pills on the right. The sentence is
	// the card: everything else supports it.
	QHBoxLayout* headlineRow = new QHBoxLayout();
	headlineRow->setContentsMargins(0, 0, 0, 0);
	headlineRow->setSpacing(10);

	headlineLabel = new QLabel(this);
	headlineLabel->setObjectName(QStringLiteral("SoftBassHeadline"));
	headlineLabel->setWordWrap(true);
	// Word-wrapped labels size to an aspect-ratio heuristic, which folded
	// the short headline in half once the pills stopped absorbing the row's
	// slack; the floor keeps a one-sentence headline on one line while a
	// long translation still wraps instead of widening the card.
	headlineLabel->setMinimumWidth(GUIHelper::scale(360.0));
	headlineLabel->setAccessibleName(tr("Bass-management summary"));
	headlineRow->addWidget(headlineLabel);

	QWidget* actionColumn = new QWidget(this);
	actionLayout = new QHBoxLayout(actionColumn);
	actionLayout->setContentsMargins(0, 0, 0, 0);
	actionLayout->setSpacing(6);
	headlineRow->addWidget(actionColumn, 0, Qt::AlignTop);
	headlineRow->addStretch(1);
	root->addLayout(headlineRow);

	// This skin's two-line grammar: the caption is the dim second line that
	// explains, warns or reports - one line, never a stack.
	captionLabel = new QLabel(this);
	captionLabel->setObjectName(QStringLiteral("SoftBassCaption"));
	captionLabel->setWordWrap(true);
	captionLabel->setAccessibleName(tr("Bass-management details"));
	root->addWidget(captionLabel);

	QHBoxLayout* pillRow = new QHBoxLayout();
	pillRow->setContentsMargins(0, 4, 0, 0);
	pillRow->setSpacing(8);
	layoutPill = makeFactPill();
	lfePill = makeFactPill();
	headroomPill = makeFactPill();
	profilePill = makeFactPill();
	pillRow->addWidget(layoutPill);
	pillRow->addWidget(lfePill);
	pillRow->addWidget(headroomPill);
	pillRow->addWidget(profilePill);
	pillRow->addStretch(1);
	root->addLayout(pillRow);
}

QLabel* SoftSubwooferRoutingCardView::makeFactPill()
{
	QLabel* pill = new QLabel(this);
	pill->setObjectName(QStringLiteral("SoftBassFactPill"));
	pill->setVisible(false);
	return pill;
}

void SoftSubwooferRoutingCardView::addActionButton(
	QAbstractButton* button)
{
	if (button == nullptr)
		return;

	button->setObjectName(QStringLiteral("SoftBassActionButton"));
	button->setCursor(Qt::PointingHandCursor);
	actionLayout->addWidget(button);
}

void SoftSubwooferRoutingCardView::applyState(
	const SubwooferRoutingCardState& state)
{
	const bool hasError = !state.errorText.isEmpty() || !state.valid;
	const bool hasWarning = !state.warningText.isEmpty();
	const bool hasCrossover =
		state.highPassHz > 0.0 || state.lowPassHz > 0.0;

	// The headline states what this line does to the sound; the caption
	// carries the one detail (or the one worry) that matters next.
	QString headline;
	QString caption;
	QString severity = QStringLiteral("normal");

	if (hasError)
	{
		headline = tr("This bass setup needs attention.");
		caption = state.errorText;
		severity = QStringLiteral("danger");
	}
	else if (!hasCrossover)
	{
		headline = tr("All speakers play the full range.");
		caption = tr("No crossover is set, so nothing is redirected "
			"to a subwoofer.");
	}
	else
	{
		// The crossover corner already lives in the card header summary;
		// repeating it here would make the sentence a dressed-up HP/LP
		// readout (review round 3). The sentence states the outcome.
		headline = tr("Bass plays on the subwoofer.");
		if (hasWarning)
		{
			caption = state.warningText;
			severity = QStringLiteral("warning");
		}
		else if (state.sourceLfePreserved)
		{
			caption = tr("The movie LFE track is kept and played "
				"at %1 dB.").arg(decibels(state.sourceLfeGainDb));
		}
		else
		{
			caption = tr("The source LFE channel is left out.");
		}
	}

	headlineLabel->setText(headline);
	captionLabel->setText(caption);
	captionLabel->setToolTip(caption);
	captionLabel->setVisible(!caption.isEmpty());
	captionLabel->setProperty("severity", severity);
	repolish(captionLabel);

	layoutPill->setText(state.layoutLabel.isEmpty()
		? tr("Unknown layout")
		: tr("%1 speakers").arg(state.layoutLabel));
	layoutPill->setVisible(true);

	lfePill->setText(tr("LFE %1 dB")
		.arg(decibels(state.sourceLfeGainDb)));
	lfePill->setVisible(state.sourceLfePreserved);

	if (std::isfinite(state.headroomTrimDb))
	{
		headroomPill->setText(state.headroomAuto
			? tr("Auto trim %1 dB")
				.arg(decibels(state.headroomTrimDb))
			: tr("Trim %1 dB")
				.arg(decibels(state.headroomTrimDb)));
		headroomPill->setVisible(true);
	}
	else
	{
		headroomPill->setVisible(false);
	}

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
	profilePill->setText(profileText);
	profilePill->setToolTip(profileText);
	profilePill->setVisible(!profileText.isEmpty());
}

void SoftSubwooferRoutingCardView::paintEvent(QPaintEvent* event)
{
	SubwooferRoutingCardView::paintEvent(event);

	if (!hasFocus())
		return;

	// Focus is a quiet halo, not a hard ring (this skin's focused grammar).
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	QColor halo(skinTokens.focusRing);
	halo.setAlpha(90);
	painter.setPen(QPen(halo, 3.0));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(
		QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 14.0, 14.0);
}
